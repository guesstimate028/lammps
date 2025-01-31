/*
 * Copyright (C) 2020 AdvanceSoft Corporation
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/mit-license.php
 */

#include "nnp_nnlayer.h"

#define SIGMOID_MAX   NNPREAL(50.0)
#define TWTANH_ALPHA  NNPREAL(0.16)

NNLayer::NNLayer(int numInpNodes, int numOutNodes, int activation, int imemory, LAMMPS_NS::Memory* memory)
{
    if (numInpNodes < 1)
    {
        stop_by_error("input size of neural network is not positive.");
    }

    if (numOutNodes < 1)
    {
        stop_by_error("output size of neural network is not positive.");
    }

    this->numInpNodes  = numInpNodes;
    this->numOutNodes  = numOutNodes;
    this->sizeBatch    = 0;
    this->sizeBatchMax = 0;

    this->activation = activation;

    this->imemory = imemory;
    this->memory  = memory;

    this->inpData = nullptr;
    this->inpGrad = nullptr;
    this->outDrv1 = nullptr;

    this->weight = new nnpreal[this->numInpNodes * this->numOutNodes];
    this->bias   = new nnpreal[this->numOutNodes];
}

NNLayer::NNLayer(int numInpNodes, int numOutNodes, int activation) :
         NNLayer(numInpNodes, numOutNodes, activation, 0, nullptr)
{
    // NOP
}

NNLayer::~NNLayer()
{
    if (this->inpData != nullptr)
    {
        this->memory->destroy(this->inpData);
    }
    if (this->inpGrad != nullptr)
    {
        this->memory->destroy(this->inpGrad);
    }
    if (this->outDrv1 != nullptr)
    {
        this->memory->destroy(this->outDrv1);
    }

    delete[] this->weight;
    delete[] this->bias;
}

void NNLayer::setSizeOfBatch(int sizeBatch)
{
    if (sizeBatch < 1)
    {
        stop_by_error("size of batch is not positive.");
    }

    this->sizeBatch = sizeBatch;

    if (this->sizeBatchMax < this->sizeBatch)
    {
        char nameInpData[64];
        char nameInpGrad[64];
        char nameOutDrv1[64];
        sprintf(nameInpData, "nnp:inpData%d", this->imemory);
        sprintf(nameInpGrad, "nnp:inpGrad%d", this->imemory);
        sprintf(nameOutDrv1, "nnp:outDrv1%d", this->imemory);

        this->sizeBatchMax = good_memory_size(this->sizeBatch);

        if (this->inpData == nullptr)
        {
            this->memory->create(this->inpData, this->numInpNodes * this->sizeBatchMax, nameInpData);
        }
        else
        {
            this->memory->grow  (this->inpData, this->numInpNodes * this->sizeBatchMax, nameInpData);
        }

        if (this->inpGrad == nullptr)
        {
            this->memory->create(this->inpGrad, this->numInpNodes * this->sizeBatchMax, nameInpGrad);
        }
        else
        {
            this->memory->grow  (this->inpGrad, this->numInpNodes * this->sizeBatchMax, nameInpGrad);
        }

        if (this->outDrv1 == nullptr)
        {
            this->memory->create(this->outDrv1, this->numOutNodes * this->sizeBatchMax, nameOutDrv1);
        }
        else
        {
            this->memory->grow  (this->outDrv1, this->numOutNodes * this->sizeBatchMax, nameOutDrv1);
        }
    }
}

void NNLayer::scanWeight(FILE* fp, bool zeroBias, int rank, MPI_Comm world)
{
    int iweight;
    int nweight = this->numInpNodes * this->numOutNodes;

    int ibias;
    int nbias = this->numOutNodes;

    int ierr;

    ierr = 0;
    if (rank == 0)
    {
        for (iweight = 0; iweight < nweight; ++iweight)
        {
            if (fscanf(fp, IFORM_F1, &(this->weight[iweight])) != 1)
            {
                ierr = 1;
                break;
            }
        }
    }

    MPI_Bcast(&ierr, 1, MPI_INT, 0, world);
    if (ierr != 0) stop_by_error("cannot scan neural network @weight");

    ierr = 0;
    if (rank == 0)
    {
        for (ibias = 0; ibias < nbias; ++ibias)
        {
            if (fscanf(fp, IFORM_F1, &(this->bias[ibias])) != 1)
            {
                ierr = 1;
                break;
            }
        }
    }

    MPI_Bcast(&ierr, 1, MPI_INT, 0, world);
    if (ierr != 0) stop_by_error("cannot scan neural network @bias");

    MPI_Bcast(&(this->weight[0]), nweight, MPI_NNPREAL, 0, world);
    MPI_Bcast(&(this->bias[0]),   nbias,   MPI_NNPREAL, 0, world);

    if (zeroBias)
    {
        for (ibias = 0; ibias < nbias; ++ibias)
        {
            this->bias[ibias] = ZERO;
        }
    }
}

void NNLayer::projectWeightFrom(NNLayer* src, int* mapInpNodes)
{
    if (src == nullptr)
    {
        stop_by_error("source layer is null.");
    }

    if (mapInpNodes == nullptr)
    {
        stop_by_error("map of input nodes is null.");
    }

    int numInpNodes1 = src ->numInpNodes;
    int numInpNodes2 = this->numInpNodes;
    int numOutNodes1 = src ->numOutNodes;
    int numOutNodes2 = this->numOutNodes;
    int activation1  = src ->activation;
    int activation2  = this->activation;

    if (numOutNodes1 != numOutNodes2)
    {
        stop_by_error("cannot projet weight, for incorrect #out-nodes.");
    }

    if (activation1 != activation2)
    {
        stop_by_error("cannot projet weight, for incorrect activation.");
    }

    int ioutNodes;
    int iinpNodes1;
    int iinpNodes2;

    #pragma omp parallel for private (ioutNodes, iinpNodes1, iinpNodes2)
    for (ioutNodes = 0; ioutNodes < numOutNodes1; ++ioutNodes)
    {
        this->bias[ioutNodes] = src->bias[ioutNodes];

        for (iinpNodes1 = 0; iinpNodes1 < numInpNodes1; ++iinpNodes1)
        {
            iinpNodes2 = mapInpNodes[iinpNodes1];

            if (0 <= iinpNodes2 && iinpNodes2 < numInpNodes2)
            {
                this->weight[iinpNodes2 + ioutNodes * numInpNodes2] =
                src ->weight[iinpNodes1 + ioutNodes * numInpNodes1];
            }
        }
    }
}

void NNLayer::goForward(nnpreal* outData) const
{
    if (outData == nullptr)
    {
        stop_by_error("outData is null.");
    }

    if (this->inpData == nullptr)
    {
        stop_by_error("inpData is null.");
    }

    if (this->sizeBatch < 1)
    {
        stop_by_error("size of batch is not positive.");
    }

    // inpData -> outData, through neural network
    nnpreal a0 = ZERO;
    nnpreal a1 = ONE;

    xgemm_("T", "N", &(this->numOutNodes), &(this->sizeBatch), &(this->numInpNodes),
           &a1, this->weight, &(this->numInpNodes), this->inpData, &(this->numInpNodes),
           &a0, outData, &(this->numOutNodes));

    int ibatch;
    int ioutNode;

    #pragma omp parallel for private (ibatch, ioutNode)
    for (ibatch = 0; ibatch < this->sizeBatch; ++ibatch)
    {
        for (ioutNode = 0; ioutNode < this->numOutNodes; ++ioutNode)
        {
            outData[ioutNode + ibatch * this->numOutNodes] += this->bias[ioutNode];
        }
    }

    // operate activation function
    this->operateActivation(outData);
}

void NNLayer::goBackward(nnpreal* outGrad, bool toInpGrad)
{
    if (outGrad == nullptr)
    {
        stop_by_error("outGrad is null.");
    }

    if (this->sizeBatch < 1)
    {
        stop_by_error("size of batch is not positive.");
    }

    // derive activation function
    int idata;
    int ndata = this->numOutNodes * this->sizeBatch;

    #pragma omp parallel for private (idata)
    for (idata = 0; idata < ndata; ++idata)
    {
        outGrad[idata] *= this->outDrv1[idata];
    }

    nnpreal a0 = ZERO;
    nnpreal a1 = ONE;

    // outGrad -> inpGrad, through neural network
    if (toInpGrad)
    {
        if (this->inpGrad == nullptr)
        {
            stop_by_error("inpGrad is null.");
        }

        xgemm_("N", "N", &(this->numInpNodes), &(this->sizeBatch), &(this->numOutNodes),
               &a1, this->weight, &(this->numInpNodes), outGrad, &(this->numOutNodes),
               &a0, this->inpGrad, &(this->numInpNodes));
    }
}

void NNLayer::operateActivation(nnpreal* outData) const
{
    if (this->outDrv1 == nullptr)
    {
        stop_by_error("outDrv1 is null.");
    }

    nnpreal x, y, z;

    int idata;
    int ndata = this->sizeBatch * this->numOutNodes;

    if (this->activation == ACTIVATION_ASIS)
    {
        #pragma omp parallel for private (idata)
        for (idata = 0; idata < ndata; ++idata)
        {
            this->outDrv1[idata] = ONE;
        }
    }

    else if (this->activation == ACTIVATION_SIGMOID)
    {
        #pragma omp parallel for private (idata, x, y, z)
        for (idata = 0; idata < ndata; ++idata)
        {
            x = outData[idata];
            if (x < -SIGMOID_MAX)
            {
                y = ZERO;
                z = ZERO;
            }
            else if (x > SIGMOID_MAX)
            {
                y = ONE;
                z = ZERO;
            }
            else
            {
                y = ONE / (ONE + exp(-x));
                z = y * (ONE - y);
            }

            outData[idata] = y;
            this->outDrv1[idata] = z;
        }
    }

    else if (this->activation == ACTIVATION_TANH)
    {
        #pragma omp parallel for private (idata, x, y, z)
        for (idata = 0; idata < ndata; ++idata)
        {
            x = outData[idata];
            y = tanh(x);
            z = ONE - y * y;

            outData[idata] = y;
            this->outDrv1[idata] = z;
        }
    }

    else if (this->activation == ACTIVATION_ELU)
    {
        #pragma omp parallel for private (idata, x, y, z)
        for (idata = 0; idata < ndata; ++idata)
        {
            x = outData[idata];
            y = (x >= ZERO) ? x : (exp(x) - ONE);
            z = (x >= ZERO) ? ONE  : (y + ONE);

            outData[idata] = y;
            this->outDrv1[idata] = z;
        }
    }

    else if (this->activation == ACTIVATION_TWTANH)
    {
        #pragma omp parallel for private (idata, x, y, z)
        for (idata = 0; idata < ndata; ++idata)
        {
            x = outData[idata];
            y = tanh(x);
            z = ONE - y * y;

            outData[idata] = y + TWTANH_ALPHA * x;
            this->outDrv1[idata] = z + TWTANH_ALPHA;
        }
    }

    else if (this->activation == ACTIVATION_GELU)
    {
        #pragma omp parallel for private (idata, x, y, z)
        for (idata = 0; idata < ndata; ++idata)
        {
            x = outData[idata];
            y = NNPREAL(0.5) * (ONE + erf(x / ROOT2));        // -> phi
            z = exp(-NNPREAL(0.5) * x * x) / ROOT2 / ROOTPI;  // -> dphi/dx

            outData[idata] = x * y;
            this->outDrv1[idata] = y + x * z;
        }
    }
}


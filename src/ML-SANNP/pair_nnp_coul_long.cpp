/*
 * Copyright (C) 2020 AdvanceSoft Corporation
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/mit-license.php
 */

#include "pair_nnp_coul_long.h"

using namespace LAMMPS_NS;

PairNNPCoulLong::PairNNPCoulLong(LAMMPS *lmp) : PairNNPCharge(lmp)
{
    ewaldflag = 1;
    pppmflag  = 1;
    ftable    = nullptr;

    this->g_ewald = 0.0;
}

PairNNPCoulLong::~PairNNPCoulLong()
{
    if (copymode)
    {
        return;
    }

    if (ftable)
    {
        free_tables();
    }
}

void PairNNPCoulLong::compute(int eflag, int vflag)
{
    bool hasGrown[3];

    double** x = atom->x;
    double** f = atom->f;
    double* q = atom->q;
    tagint *tag = atom->tag;
    int nlocal = atom->nlocal;
    int newton_pair = force->newton_pair;

    int inum = list->inum;
    int* ilist = list->ilist;
    int* numneigh = list->numneigh;
    int** firstneigh = list->firstneigh;

    double r, rr;
    double rcut = this->property->getRcutoff();

    double delx, dely, delz;
    double fx, fy, fz;

    int i, j;
    int ii, jj, jnum;
    int *jlist;
    tagint itag, jtag;
    double xtmp, ytmp, ztmp, qtmp;
    double rinv, r2inv;
    double ecoul, fpair;
    double forcecoul, factor_coul, fc, dfcdr;

    double *special_coul = force->special_coul;
    double qqrd2e = force->qqrd2e;

    int itable;
    double fraction, table;
    double grij, expm2, prefactor, t, erfc;

    ev_init(eflag, vflag);

    prepareNN(hasGrown);

    performNN(eflag);

    computeLJLike(eflag);

    #pragma omp parallel for private(i, j, ii, jj, jnum, jlist, itag, jtag, xtmp, ytmp, ztmp, qtmp, \
                                     factor_coul, r, rr, rinv, r2inv, forcecoul, fc, dfcdr, ecoul, fpair, \
                                     itable, fraction, table, grij, expm2, prefactor, t, erfc)
    for (ii = 0; ii < inum; ii++)
    {
        i = ilist[ii];
        itag = tag[i];
        xtmp = x[i][0];
        ytmp = x[i][1];
        ztmp = x[i][2];
        qtmp = q[i];

        jlist = firstneigh[i];
        jnum  = numneigh[i];

        for (jj = 0; jj < jnum; jj++)
        {
            this->frcNeighborAll[ii][jj][0] = -1.0;
            this->frcNeighborAll[ii][jj][1] =  0.0;
            this->frcNeighborAll[ii][jj][2] =  0.0;

            j = jlist[jj];
            factor_coul = special_coul[sbmask(j)];
            j &= NEIGHMASK;

            // skip half of atoms
            jtag = tag[j];
            if (itag > jtag) {
                if ((itag + jtag) % 2 == 0) continue;
            } else if (itag < jtag) {
                if ((itag + jtag) % 2 == 1) continue;
            } else {
                if (x[j][2] < ztmp) continue;
                if (x[j][2] == ztmp && x[j][1] < ytmp) continue;
                if (x[j][2] == ztmp && x[j][1] == ytmp && x[j][0] < xtmp) continue;
            }

            r = this->posNeighborAll[ii][jj][0];
            if (r <= 0.0) continue;

            if (r < rcut)
            {
                rinv = 1.0 / r;
                forcecoul = qqrd2e * qtmp * q[j] * rinv;

                fc = 0.5 * (cos(PId * r / rcut) + 1.0);
                dfcdr = -0.5 * PId / rcut * sin(PId * r / rcut);
                fpair = factor_coul * forcecoul * (rinv * fc - dfcdr) * rinv;

                ecoul = eflag ? (factor_coul * forcecoul * fc) : 0.0;

                this->frcNeighborAll[ii][jj][0]  = 1.0;
                this->frcNeighborAll[ii][jj][1] -= ecoul;
                this->frcNeighborAll[ii][jj][2] -= fpair;
            }

            if (r < this->cutcoul)
            {
                rr = r * r;
                r2inv = 1.0 / rr;
                if (!ncoultablebits || rr <= tabinnersq)
                {
                    grij = g_ewald * r;
                    expm2 = exp(-grij * grij);
                    t = 1.0 / (1.0 + EWALD_P * grij);
                    erfc = t * (A1 + t * (A2 + t * (A3 + t * (A4 + t * A5)))) * expm2;
                    prefactor = qqrd2e * qtmp * q[j] / r;
                    forcecoul = prefactor * (erfc + EWALD_F * grij * expm2);
                    if (factor_coul < 1.0)
                        forcecoul -= (1.0 - factor_coul) * prefactor;
                }
                else
                {
                    union_int_float_t rsq_lookup;
                    rsq_lookup.f = rr;
                    itable = rsq_lookup.i & ncoulmask;
                    itable >>= ncoulshiftbits;
                    fraction = (rsq_lookup.f - rtable[itable]) * drtable[itable];
                    table = ftable[itable] + fraction * dftable[itable];
                    forcecoul = qtmp * q[j] * table;
                    if (factor_coul < 1.0)
                    {
                        table = ctable[itable] + fraction * dctable[itable];
                        prefactor = qtmp * q[j] * table;
                        forcecoul -= (1.0 - factor_coul) * prefactor;
                    }
                }

                fpair = forcecoul * r2inv;

                ecoul = 0.0;

                if (eflag)
                {
                    if (!ncoultablebits || rr <= tabinnersq)
                    {
                        ecoul = prefactor * erfc;
                    }
                    else
                    {
                        table = etable[itable] + fraction * detable[itable];
                        ecoul = qtmp * q[j] * table;
                    }

                    if (factor_coul < 1.0)
                    {
                        ecoul -= (1.0 - factor_coul) * prefactor;
                    }
                }

                this->frcNeighborAll[ii][jj][0]  = 1.0;
                this->frcNeighborAll[ii][jj][1] += ecoul;
                this->frcNeighborAll[ii][jj][2] += fpair;
            }
        }
    }

    for (ii = 0; ii < inum; ii++)
    {
        i = ilist[ii];

        jlist = firstneigh[i];
        jnum  = numneigh[i];

        for (jj = 0; jj < jnum; jj++)
        {
            if (this->frcNeighborAll[ii][jj][0] > 0.0)
            {
                j = jlist[jj];
                j &= NEIGHMASK;

                delx = -this->posNeighborAll[ii][jj][1];
                dely = -this->posNeighborAll[ii][jj][2];
                delz = -this->posNeighborAll[ii][jj][3];

                ecoul = this->frcNeighborAll[ii][jj][1];
                fpair = this->frcNeighborAll[ii][jj][2];

                fx = delx * fpair;
                fy = dely * fpair;
                fz = delz * fpair;

                f[i][0] += fx;
                f[i][1] += fy;
                f[i][2] += fz;

                f[j][0] -= fx;
                f[j][1] -= fy;
                f[j][2] -= fz;

                if (evflag)
                {
                    ev_tally(i, j, nlocal, newton_pair,
                             0.0, ecoul, fpair, delx, dely, delz);
                }
            }
        }
    }

    if (vflag_fdotr)
    {
        virial_fdotr_compute();
    }
}

void PairNNPCoulLong::init_style()
{
    PairNNPCharge::init_style();

    // insure use of KSpace long-range solver, set g_ewald

    if (force->kspace == nullptr)
    {
        error->all(FLERR, "Pair style requires a KSpace style");
    }
    if (force->kspace->get_gewaldflag() == 0)
    {
        error->all(FLERR, "Must use 'kspace_modify gewald' with Pair style nnp/coul/long");
    }

    this->g_ewald = force->kspace->g_ewald;

    // setup force tables

    if (ncoultablebits)
    {
        init_tables(this->cutcoul, nullptr);
    }
}


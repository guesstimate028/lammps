/*
 * Copyright (C) 2020 AdvanceSoft Corporation
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/mit-license.php
 */

#ifndef NEURAL_NETWORK_POTENTIAL_H_
#define NEURAL_NETWORK_POTENTIAL_H_

#include "nnp_common.h"
#include "nnp_property.h"
#include "nnp_symm_func.h"
#include "nnp_symm_func_manybody.h"
#ifdef _NNP_GPU
#include "nnp_symm_func_gpu.h"
#include "nnp_symm_func_gpu_behler.h"
#include "nnp_symm_func_gpu_chebyshev.h"
#else
#include "nnp_symm_func_behler.h"
#include "nnp_symm_func_chebyshev.h"
#endif
#include "nnp_reax_param.h"
#include "nnp_reax_pot.h"
#include "nnp_nnlayer.h"
#include "nnp_nnarch.h"

#endif /* NEURAL_NETWORK_POTENTIAL_H_ */

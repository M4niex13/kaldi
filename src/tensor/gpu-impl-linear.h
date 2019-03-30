// tensor/gpu-impl-linear.h

// Copyright      2019  Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#ifndef KALDI_TENSOR_GPU_IMPL_LINEAR_H_
#define KALDI_TENSOR_GPU_IMPL_LINEAR_H_ 1

#include "tensor/tensor.h"


// This header actually contains implementations of functions that are required
// by tensor-impl-linear.cc.  It should not be included by users of this
// library.


namespace kaldi {
namespace tensor {


template <typename Real>
inline static void AddProductScalar3GPU(
    float alpha, float beta,
    const TensorImpl &a, const TensorImpl &b, const TensorImpl *c) {
  // TODO: make this actually work on GPU, probably by calling the 1-d vector version.
  Real *a_data = static_cast<Real*>(a->data),
      *b_data = static_cast<Real*>(b->data),
      *c_data = static_cast<Real*>(c->data);
  if (beta != 0.0) {
    *c_data = (beta * *c_data) + alpha * (*a_data + *b_data);
  } else {  // don't propagate NaN
    *c_data = alpha * (*a_data + *b_data);
  }
}




}



}  // namespace tensor
}  // namespace kaldi


#endif  // KALDI_TENSOR_GPU_IMPL_LINEAR_H_

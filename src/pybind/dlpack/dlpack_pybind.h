// pybind/dlpck/dlpack_pybind.h

// Copyright 2019   Mobvoi AI Lab, Beijing, China
//                  (author: Fangjun Kuang, Yaguang Hu, Jian Wang)

// See ../../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#ifndef KALDI_PYBIND_DLPACK_DLPACK_PYBIND_H_
#define KALDI_PYBIND_DLPACK_DLPACK_PYBIND_H_

#include "pybind/kaldi_pybind.h"

#include "dlpack/dlpack_submatrix.h"
#include "dlpack/dlpack_subvector.h"

void pybind_dlpack(py::module& m);

namespace kaldi {

py::capsule VectorToDLPack(VectorBase<float>* v);
py::capsule MatrixToDLPack(MatrixBase<float>* m);
py::capsule CuVectorToDLPack(CuVectorBase<float>* v);
py::capsule CuMatrixToDLPack(CuMatrixBase<float>* m);

DLPackSubVector<float>* SubVectorFromDLPack(py::capsule* capsule);
DLPackSubMatrix<float>* SubMatrixFromDLPack(py::capsule* capsule);
DLPackCuSubVector<float>* CuSubVectorFromDLPack(py::capsule* capsule);
DLPackCuSubMatrix<float>* CuSubMatrixFromDLPack(py::capsule* capsule);

}  // namespace kaldi

#endif  // KALDI_PYBIND_DLPACK_DLPACK_PYBIND_H_

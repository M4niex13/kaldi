// pybind/matrix/matrix_pybind.cc

// Copyright 2019   Daniel Povey
//           2019   Dongji Gao
//           2019   Mobvoi AI Lab, Beijing, China (author: Fangjun Kuang)

// See ../../../COPYING for clarification regarding multiple authors
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

#include "matrix/matrix_pybind.h"

#include "dlpack/dlpack_deleter.h"
#include "matrix/kaldi-matrix.h"

using namespace kaldi;

void pybind_matrix(py::module& m) {
  py::class_<MatrixBase<float>,
             std::unique_ptr<MatrixBase<float>, py::nodelete>>(
      m, "FloatMatrixBase",
      "Base class which provides matrix operations not involving resizing\n"
      "or allocation.   Classes Matrix and SubMatrix inherit from it and take "
      "care of allocation and resizing.")
      .def("NumRows", &MatrixBase<float>::NumRows, "Return number of rows")
      .def("NumCols", &MatrixBase<float>::NumCols, "Return number of columns")
      .def("Stride", &MatrixBase<float>::Stride, "Return stride")
      .def("__repr__",
           [](const MatrixBase<float>& b) -> std::string {
             std::ostringstream str;
             b.Write(str, false);
             return str.str();
           })
      .def("__getitem__",
           [](const MatrixBase<float>& m, std::pair<ssize_t, ssize_t> i) {
             return m(i.first, i.second);
           })
      .def("__setitem__",
           [](MatrixBase<float>& m, std::pair<ssize_t, ssize_t> i, float v) {
             m(i.first, i.second) = v;
           })
      .def("numpy",
           [](MatrixBase<float>* m) {
             return py::array_t<float>(
                 {m->NumRows(), m->NumCols()},  // shape
                 {sizeof(float) * m->Stride(),
                  sizeof(float)},  // stride in bytes
                 m->Data(),        // ptr
                 py::none());      // pass a base object to avoid copy!
           })
      .def("to_dlpack", [](MatrixBase<float>* m) {
        // we use the name `to_dlpack` because PyTorch uses the same name

        // the created `managed_tensor` will be freed in
        // `DLManagedTensorDeleter`, which does not free `data`,
        // so no memory leak here
        auto* managed_tensor = new DLManagedTensor();
        managed_tensor->manager_ctx = nullptr;

        // setup the deleter to free allocated memory.
        // refer to
        // https://github.com/pytorch/pytorch/blob/master/torch/csrc/Module.cpp#L361
        // for how and when the deleter is invoked.
        managed_tensor->deleter = &DLManagedTensorDeleter;

        auto* tensor = &managed_tensor->dl_tensor;
        tensor->data = m->Data();
        tensor->ctx.device_type = kDLCPU;
        tensor->ctx.device_id = 0;

        tensor->ndim = 2;

        tensor->dtype.code = kDLFloat;
        tensor->dtype.bits = 32;  // single precision float
        tensor->dtype.lanes = 1;

        // `shape` and `strides` are freed in `DLManagedTensorDeleter`, so
        // no memory leak here
        tensor->shape = new int64_t[2];
        tensor->shape[0] = m->NumRows();
        tensor->shape[1] = m->NumCols();

        tensor->strides = new int64_t[2];
        tensor->strides[0] = m->Stride();
        tensor->strides[1] = 1;
        tensor->byte_offset = 0;

        // WARNING(fangjun): the name of the capsule MUST be `dltensor` for
        // PyTorch; refer to
        // https://github.com/pytorch/pytorch/blob/master/torch/csrc/Module.cpp#L383/
        // for more details.
        return py::capsule(managed_tensor, "dltensor");
      });

  py::class_<Matrix<float>, MatrixBase<float>>(m, "FloatMatrix",
                                               pybind11::buffer_protocol())
      .def_buffer([](const Matrix<float>& m) -> pybind11::buffer_info {
        return pybind11::buffer_info(
            (void*)m.Data(),  // pointer to buffer
            sizeof(float),    // size of one scalar
            pybind11::format_descriptor<float>::format(),
            2,                           // num-axes
            {m.NumRows(), m.NumCols()},  // buffer dimensions
            {sizeof(float) * m.Stride(),
             sizeof(float)});  // stride for each index (in chars)
      })
      .def(py::init<const MatrixIndexT, const MatrixIndexT, MatrixResizeType,
                    MatrixStrideType>(),
           py::arg("row"), py::arg("col"), py::arg("resize_type") = kSetZero,
           py::arg("stride_type") = kDefaultStride);

  py::class_<SubMatrix<float>, MatrixBase<float>>(m, "FloatSubMatrix")
      .def(py::init([](py::buffer b) {
        py::buffer_info info = b.request();
        if (info.format != py::format_descriptor<float>::format()) {
          KALDI_ERR << "Expected format: "
                    << py::format_descriptor<float>::format() << "\n"
                    << "Current format: " << info.format;
        }
        if (info.ndim != 2) {
          KALDI_ERR << "Expected dim: 2\n"
                    << "Current dim: " << info.ndim;
        }

        // numpy is row major by default, so we use strides[0]
        return new SubMatrix<float>(reinterpret_cast<float*>(info.ptr),
                                    info.shape[0], info.shape[1],
                                    info.strides[0] / sizeof(float));
      }))
      .def("from_dlpack", [](py::capsule* capsule) {
        DLManagedTensor* managed_tensor = *capsule;

        auto* tensor = &managed_tensor->dl_tensor;

        // we support only 2-D tensor
        KALDI_ASSERT(tensor->ndim == 2);

        // we support only float (single precision, 32-bit) tensor
        KALDI_ASSERT(tensor->dtype.code == kDLFloat);
        KALDI_ASSERT(tensor->dtype.bits == 32);
        KALDI_ASSERT(tensor->dtype.lanes == 1);

        auto* ctx = &tensor->ctx;
        KALDI_ASSERT(ctx->device_type == kDLCPU);

        // DLPack assumes row major, so we use strides[0]
        return SubMatrix<float>(reinterpret_cast<float*>(tensor->data),
                                tensor->shape[0], tensor->shape[1],
                                tensor->strides[0]);
      });

  py::class_<Matrix<double>, std::unique_ptr<Matrix<double>, py::nodelete>>(
      m, "DoubleMatrix",
      "This bind is only for internal use, e.g. by OnlineCmvnState.")
      .def(py::init<const Matrix<float>&>(), py::arg("src"));
}

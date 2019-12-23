// pybind/matrix/vector_pybind.cc

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

#include "matrix/vector_pybind.h"

#include "dlpack/dlpack_deleter.h"
#include "matrix/kaldi-vector.h"

using namespace kaldi;

void pybind_vector(py::module& m) {
  py::class_<VectorBase<float>,
             std::unique_ptr<VectorBase<float>, py::nodelete>>(
      m, "FloatVectorBase",
      "Provides a vector abstraction class.\n"
      "This class provides a way to work with vectors in kaldi.\n"
      "It encapsulates basic operations and memory optimizations.")
      .def("Dim", &VectorBase<float>::Dim,
           "Returns the dimension of the vector.")
      .def("__repr__",
           [](const VectorBase<float>& v) -> std::string {
             std::ostringstream str;
             v.Write(str, false);
             return str.str();
           })
      .def("__getitem__",
           [](const VectorBase<float>& v, int i) { return v(i); })
      .def("__setitem__",
           [](VectorBase<float>& v, int i, float val) { v(i) = val; })
      .def("numpy",
           [](VectorBase<float>* v) {
             return py::array_t<float>(
                 {v->Dim()},       // shape
                 {sizeof(float)},  // stride in bytes
                 v->Data(),        // ptr
                 py::none());      // pass a base object to avoid copy!
             // (fangjun): the base object can be anything containing a non-null
             // ptr. I cannot think of a better way than to pass a `None`
             // object.
             //
             // `numpy` instead of `Numpy`, `ToNumpy` or `SomeOtherName` is used
             // here because we want to follow the style in PyKaldi and PyTorch
             // using  the  method `numpy()` to convert a matrix/tensor to a
             // numpy array.
           })
      .def("to_dlpack", [](VectorBase<float>* v) {
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
        tensor->data = v->Data();
        tensor->ctx.device_type = kDLCPU;
        tensor->ctx.device_id = 0;

        tensor->ndim = 1;

        tensor->dtype.code = kDLFloat;
        tensor->dtype.bits = 32;  // single precision float
        tensor->dtype.lanes = 1;

        // `shape` and `strides` are freed in `DLManagedTensorDeleter`, so
        // no memory leak here .
        tensor->shape = new int64_t[1];
        tensor->shape[0] = v->Dim();

        tensor->strides = new int64_t[1];
        tensor->strides[0] = 1;
        tensor->byte_offset = 0;

        // WARNING(fangjun): the name of the capsule MUST be `dltensor` for
        // PyTorch; refer to
        // https://github.com/pytorch/pytorch/blob/master/torch/csrc/Module.cpp#L383/
        // for more details
        return py::capsule(managed_tensor, "dltensor");
      });

  py::class_<Vector<float>, VectorBase<float>>(m, "FloatVector",
                                               py::buffer_protocol())
      .def_buffer([](const Vector<float>& v) -> py::buffer_info {
        return py::buffer_info((void*)v.Data(), sizeof(float),
                               py::format_descriptor<float>::format(),
                               1,  // num-axes
                               {v.Dim()},
                               {sizeof(float)});  // strides (in chars)
      })
      .def(py::init<const MatrixIndexT, MatrixResizeType>(), py::arg("size"),
           py::arg("resize_type") = kSetZero);

  py::class_<SubVector<float>, VectorBase<float>>(m, "FloatSubVector")
      .def(py::init([](py::buffer b) {
        py::buffer_info info = b.request();
        if (info.format != py::format_descriptor<float>::format()) {
          KALDI_ERR << "Expected format: "
                    << py::format_descriptor<float>::format() << "\n"
                    << "Current format: " << info.format;
        }
        if (info.ndim != 1) {
          KALDI_ERR << "Expected dim: 1\n"
                    << "Current dim: " << info.ndim;
        }
        return new SubVector<float>(reinterpret_cast<float*>(info.ptr),
                                    info.shape[0]);
      }))
      .def("from_dlpack", [](py::capsule* capsule) {
        DLManagedTensor* managed_tensor = *capsule;
        // (fangjun): the above assignment will either throw or succeed with a
        // non-null ptr so no need to check for nullptr below

        auto* tensor = &managed_tensor->dl_tensor;

        // we support only 1-D tensor
        KALDI_ASSERT(tensor->ndim == 1);

        // we support only float (single precision, 32-bit) tensor
        KALDI_ASSERT(tensor->dtype.code == kDLFloat);
        KALDI_ASSERT(tensor->dtype.bits == 32);
        KALDI_ASSERT(tensor->dtype.lanes == 1);

        auto* ctx = &tensor->ctx;
        KALDI_ASSERT(ctx->device_type == kDLCPU);

        return SubVector<float>(reinterpret_cast<float*>(tensor->data),
                                tensor->shape[0]);

      });
}

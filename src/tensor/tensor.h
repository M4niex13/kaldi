// tensor/tensor.h

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

#ifndef KALDI_TENSOR_TENSOR_H_
#define KALDI_TENSOR_TENSOR_H_ 1

#include "tensor/tensor-common.h"
#include "tensor/tensor-pattern.h"
#include "tensor/storage.h"

namespace kaldi {
namespace tensor {


/**
   TensorImpl is basically a Tensor without the shared_ptr to Storage
   (which is expensive to pass around, because of the cost of atomics).
   The Tensor contains it as a member, rather than as a pointer.

   Most of our internal functions use TensorImpl rather than Tensor because
   it is easier to manipulate, but you need to know what you are doing.
*/
struct TensorImpl {
  TensorPattern pattern;
  DataType dtype;
  Device device;
  void *data{nullptr};
};

// Metadata for a Tensor.  It's occasionally convenient to have this
// in a struct.
struct TensorMeta {
  TensorPattern pattern;
  DataType dtype;
  Device device;
  // Note: the offset is only used in some situations,
  // it's
  // We may turn this into an offset measured in elements.
  int32 offset;
};

/**
   A Tensor is a multi-dimensional array (up to 5 dimensions) of types such as
   float or double (and eventually ints).  Multiple Tensors may point to data
   allocated from the same Storage.  Class Tensor contains enough elements that
   it makes sense most of the time to pass it around by reference (Tensor&) or
   by pointer (e.g. Tensor* or std::shared_pointer<Tensor>).  This is unlike
   in PyTorch where there is a separate TensorImpl class and Tensor really just
   contains a pointer to it.

   Most of the operations that you would do on a Tensor (like addition,
   multiplication and so on) are declared out-of-line in tensor-functions.h.
 */
class Tensor {
 public:

  inline bool Initialized() { return data_ != NULL; }

  /// Return the number of axes (a number in {0,1,2,3,4}).  In mathematical
  // contexts, this is sometimes known as the rank of the tensor, or sometimes
  // even its dimension, but these terms are ambiguous so we avoid them, and use
  // the terms 'number of axes' or 'axis' throughout.
  inline int32 NumAxes() const { return impl_.pattern.num_axes; }


  const TensorImpl &Impl() { return impl_; }

  // Return reference to the struct containing the dimension and
  // stride info.
  const TensorPattern &Pattern() const { return impl_.pattern; }

  // Return an array containing dimensions of the tensor; equivalent to
  // .shape in PyTorch.  Dims().size() will equal NumAxes().
  // We limit each dimension to int32, because BLAS's interface uses int,
  // which on many common 64-bit platforms is configured with 32 bits.
  // However the product of dimensions may still be 64 bits.
  inline ArrayRef<int32> Dims() const { return ArrayRef{impl_.pattern.num_axes, impl_.pattern_.dims}; }

  // Returns the dimension on this axis, a number >= 1.  Result is
  // undefined if axis < 0 or axis >= NumAxes().
  inline int32 Dim(int32 axis) const { return impl_.pattern.dims[axis]; }

  // Returns an array containing the strides of the tensor.
  // Strides().size() will equal NumAxes().
  inline ArrayRef<int32> Strides() const { return ArrayRef{impl_.pattern.num_axes, impl_.pattern.strides}; }

  // Returns the stride on this axis.  Will be zero if the corresponding
  // dimension is 1, and otherwise nonzero (but not necessarily positive).
  inline int32 Stride(int32 axis) const { return impl_.pattern.strides[axis]; }

  // Returns the number of elements in the Tensor; must be > 0.
  inline int64 NumElements() const { return derived_.num_elements; }

  // Returns true if the data forms a contiguous block in memory.
  // (not the same as 'contiguous()' in PyTorch, which also requires
  // that the strides be 'C'-style.
  inline bool IsContiguous() const { return derived_.is_contiguous; }

  // Returns true if the strides for this array are what you would
  // expect if you were to construct a Tensor from this->Dims();
  // this means "C"-style strides, except that any axis with dimension=1
  // has its stride set to zero.  This is our equivalent of PyTorch's
  // contiguous().
  inline bool HasCStrides() const { return derived_.has_c_strides; }

  // Return the data type.
  DataType Dtype() const { return dtype_; }

  // Indexing operators.  All of these return Tensors which reference the same
  // underlying data as the original Tensor.  We could have done this with just
  // a single indexing operator taking 5 args of type RangeExt defaulting to
  // `all`, but we provide separate versions for each num-args for efficiency.
  // You can provide an int32 where RangeExt is expected; it will be
  // converted to a special struct of type Range. See the documentation for type
  // Range, and the table which it contains.  If a is a Tensor with 1 axis, a(0)
  // will return a scalar Tensor (0 axes
  //
  // Any of these indexing operators can operate on Tensors with more axes;
  // trailing axes will be left alone.

  // this operator () taking int32 is only provided in the one-arg case as a
  // convenience; in any case, RangeExt can be constructed from int32 with the
  // same effect.
  Tensor operator () (int32 i0) const;
  Tensor operator () (RangeExt s0) const;
  Tensor operator () (RangeExt s0, RangeExt s1) const;
  Tensor operator () (RangeExt s0, RangeExt s1, RangeExt s2) const;
  Tensor operator () (RangeExt s0, RangeExt s1, RangeExt s2,
                      RangeExt s3) const;
  // A particularly complicated example showing what is possible:
  // Tensor a(...);
  // Tensor b = a(all,10,Range(0,5),Range(all,all,-1),all)
  Tensor operator () (RangeExt s0, RangeExt s1, RangeExt s2,
                      RangeExt s3, RangeExt s4) const;


  // For a scalar Tensor (NumAxes() == 0) returns the item, cast to
  // float (if it was not already float); throws if NumAxes() > 0.
  explicit operator float() const;
  // For a scalar Tensor (NumAxes() == 0) returns the item, cast to
  // double (if it was not already double); throws if NumAxes() > 0.
  explicit operator double() const;
  // For a scalar Tensor (NumAxes() == 0) returns the item, cast to
  // int32 (if it was not already int32); throws if NumAxes() > 0.
  explicit operator int32() const;

  // For a Tensor storing floats, returns the data pointer cast to float;
  // otherwise, throws.  (note: this is const only as it doesn't change the
  // Tensor meta-info, but you could change the data using the pointer).
  explicit operator float* () const;
  // For a Tensor storing doubles, returns the data pointer cast to float;
  // otherwise, throws.  (note: this is const only as it doesn't change the
  // Tensor meta-info, but you could change the data using the pointer).
  explicit operator double* () const;

  // Assignment operation which sets all elements to a constant.  Valid
  // for Tensors of any floating point type.
  const Tensor & operator = (float f);

  // Transpose the two axes by swapping their dims and strides without changing
  // the underlying data in memory.  This modifies *this;
  // Negative axes are allowed, and interpreted as NumAxes() - axis.
  void Transpose(int32 axis1 = 0, int32 axis2 = 1);


  // Constructor which does not really initialize the Tensor.  impl_.pattern,
  // derived_ and dtype_ may contain nonsense.
  Tensor(): data_(NULL) { }

  // Copy constructor that copies the metadata while sharing the underlying
  // data.
  Tensor (const Tensor &other) = default;

  // Move assignment.  Does not copy the data.
  Tensor(Tensor &&other);

  /**
     Construct a new Tensor with freshly allocated underlying data with
     the data type, device and dimension the same as `other`.

       @param [in]  other  The tensor that we are taking metadata from (we
                    are not sharing its underlying data).
       @param [in]  sp   The stride policy; if kCopyStrides then we use
                       strides with the same sign and size-order as
                       `other`, while filling in any gaps if `other`
                       was not contiguous, if kCstrides then we use
                       "C" style strides for any dimensions != 1.
       @param [in]  ip   The data initialization policy
  */
  Tensor(const Tensor &other, StridePolicy sp, InitializePolicy ip);



  /** Construct a Tensor with freshly allocated data.
       @param [in] dims    The dimensions of the tensor (zero to 5
                    positive integers).
       @param [in] dtype   The data type to use
       @param [in] device  The device to put the data on
       @param [in] set_zero   If true, set the tensor to zero.  If false,
                        the contents will be undefined.
   */
  Tensor(ArrayRef<int32> dims, DataType dtype, Device device,
         bool set_zero = false);

  /**
     Construct a Tensor with the dimensions and strides provided.  This differs
     from the constructor taking `ArrayRef<int32> dims` in that it will use
     the strides in `pattern` (except that if the data in `pattern` is not
     contiguous, it will make it contiguous by filling in any gaps).  This means
     that, for example, if you use this constructor on a 2-dimensional Tensor
     that has been transposed and thus has a column-major layout, the resulting
     Tensor will also have a column-major layout.

       @param [in] pattern  The dimension and stride information that
                  this tensor should match (although we will fill gaps
                  to make it contiguous)
       @param [in] dtype   The data type to use
       @param [in] device  The device to put the data on
       @param [in] set_zero   If true, set the data to zero.  If false,
                        the contents will be undefined.

  */
  Tensor(TensorPattern &pattern, DataType dtype, Device device,
         InitializePolicy p);

  /**
     Construct a Tensor from the metadata in 'meta'.  Requires
     that meta.pattern be contiguous (meaning: literally contiguous,
     not the PyTorch meaning which is a stronger condition).
     ??Possibly we could make it similar to the constructor above
       and have it just make it contiguous if it was not.??


       @param [in] meta  Struct containing the metadata specifying
                     the Tensor's pattern, data-type and device

                     ;pattern  The dimension and stride information that
                  this tensor should match (although we will fill gaps
                  to make it contiguous)
       @param [in] dtype   The data type to use
       @param [in] device  The device to put the data on
       @param [in] set_zero   If true, set the data to zero.  If false,
                        the contents will be undefined.

  */
  Tensor(TensorMeta &meta, InitializePolicy p);


  /**
     This constructor, which is intended for use primarily in internal
     code and
   */
  Tensor(TensorPattern &pattern, DataType dtype, Device device,
         void *data_);

 private:
  // The tensor dim and strides.
  TensorImpl impl_;

  // The raw data pointer.  Will be cast to a pointer of the appropriate
  // type before indexing.
  void *data_;

  // The storage region where the data resides.  data_ does not necessarily
  // equal storage_->data; it may be more than that, e.g. if this is a view
  // to part of another Tensor.
  std::shared_ptr<Storage> storage_;
};




}  // namespace tensor
}  // namespace kaldi


#endif  // KALDI_TENSOR_TENSOR_H_

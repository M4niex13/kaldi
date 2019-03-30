// tensor/tensor-pattern-utils.h

//  Copyright      2019  Johns Hopkins University (author: Daniel Povey)

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


#include "tensor/tensor-common.h"
#include "tensor/array-ref.h"

/**
   This is some notes on plans for kaldi10 tensor stuff, nothing is fully fleshed out.
*/

namespace kaldi {
namespace tensor {


/**
   This function returns a code that compactly says whether each axis
   has dim = 1 or dim != 1.  For purposes of the code generated, the number
   of axes does not matter.  The lower-order KALDI_TENSOR_MAX_DIM bits
   of the code might potentially be set; the rest will be zero.

   The rightmost (least significant) bit corresponds to the last-numbered axis,
   equivalent to raxis (reversed axis-index) == 0.

   Note that non of the example `dims` vectors below have any leading
   (dim=1) axes, because they wouldn't affect the code.

   The examples below will use c++14 binary literals, although
   the code doesn't use them.  In the notation below, in dims vectors,
   x is a stand-in for 'any number greater than 1'.

    0b00000000  0x00  dims=(), a scalar
    0b00000001  0x01  dims=(x)
    0b00000010  0x02  dims=(x,1)
    0b00000011  0x03  dims=(x,x)

    etc.

  See also GetPatternCode(), which includes the same information but
  also stride-related information.
 */
int32 GetDimsCode(const TensorPattern &pattern);


enum PatternEnum {
  kPatternContainsNegativeStride = 2048
  // e.g.:
  // bool contains_negative_stride =
  //     (pattern.code | kPatternContainsNegativeStride) != 0;
};

// Returns true if the pattern code indicates that the pattern contains a
// negative stride.
inline bool ContainsNegativeStride(int32 pattern_code) {
  return (pattern_code | kPatternContainsNegativeStride) != 0;
}

// Returns true if the pattern code indicates that the raxis
// numbered 'raxis' (the r refers to the backwards numbering used
// in 'pattern') is 'trivial' (meaning: dim=1, stride=0).
inline bool AxisIsTrivial(int32 pattern_code, int32 raxis) {
  return (pattern_code | 1 << raxis) == 0;
}


/**
   This function returns a code that compactly represents the same information
   as GetDimsCode() [i.e. which axes had dim != 1], but also encodes which axis,
   if any, had stride=1, and has a bit that says whether any axis had negative
   stride.  (No two axes can have stride=1, due to the uniqueness rule; search
   in tensor-pattern.h).

   Let
      n = 0 if no axis had stride=1, otherwise:
      n = 1 + the raxis index which had stride=1.

    (raxis is the axis index when accessing the axes in reversed order, as
     stored in pattern.dims and pattern.strides).

   For example if the strides were [10,3,1] we would have
   n = 1; i if the strides were [10,1,3] we would have n = 2.

   IMPORTANT NOTE ON ORDERING: lists of dims or strides in square
   brackets, like [1,2], are in the non-reversed ordering as exposed
   by the Tensor API.

   The value 'n' occupies the bits starting from 8 in the returned code,
   i.e. bits 8,9,10 (counting from the right, i.e. from the least to
   most significant).

   Bit 11 is 1 if any of the strides were negative, and zero otherwise.
   None of the example bit-patterns below have this bit set.  The
   underlying BLAS in most cases does not support negative strides so
   we deal with it by copying the data to a temporary with positive
   strides.

   The low-order KALDI_TENSOR_MAX_DIM bits are as returned by GetDimsCode().

   The explanation below will use c++14 binary literals (like 0b010101), although the code
   doesn't use them as we compile as c++11; we show the corresponding hex codes which
   are used in the code (and anyway easier to parse).

   In the notation below, in dims vectors, x or X is a stand-in for 'any number
   not equal to 1', and upper-case X indicates that the axis has stride=1.  In
   the example `dims` vectors below, we don't put any leading `dim=1` axes,
   because they would not affect the code generated.  The list of numbers
   in square brackets [] below may be interpreted as the sequence of dims for the
   Tensor, in the non-reversed ordering that the Tensor API exposes.

   The ' at the 8th bit is to make the bit-string easier to parse.

    0b000'00000000  0x000  dims=[], a scalar
    0b000'00000001  0x001  dims=[x], a vector with a stride
    0b001'00000001  0x101  dims=[X], a vector
    0b000'00000010  0x002  dims=[x,1], a vector with a stride
    0b010'00000010  0x202  dims=[X,1], a vector
    0b000'00000011  0x003  dims=[x,x], a matrix with a stride
    0b001'00000011  0x103  dims=[x,X], a matrix
    0b010'00000011  0x203  dims=[X,x], a transposed matrix
    0b000'00000100  0x008  dims=[x,1,1], a vector with a stride
    0b011'00000100  0x308  dims=[X,1,1], a vector
    0b010'00000110  0x20B  dims=[x,X,1], a matrix
    0b011'00000110  0x30B  dims=[X,x,1], a transposed matrix
    0b000'00000110  0x10B  dims=[x,x,1], a matrix with column stride
    0b001'00000101  0x109  dims=[x,1,X], a matrix
    0b011'00000101  0x309  dims=[X,1,x], a transposed matrix
    0b000'00000101  0x009  dims=[x,1,x], a matrix with column stride

    ...
 */
int32 ComputePatternCode(const TensorPattern &pattern);


inline int32 CombineCodes(int32 code1, int32 code2) {
  return (code1 << 12) | code2;
}

inline int64 CombineCodes(int32 code1, int32 code2, int32 code3) {
  return (static_cast<int64>(code1) << 24) |
      static_cast<int64>(code2 << 12) |
      static_cast<int64>(code3);
}


/**
   Modifies 'p' in-place by inserting an axis with (dim=1,stride=0) at the
   specified position specified in the reversed numbering physically used
   in the pattern.  Updates p->code.

   Showing just the dims in the pattern (in the order physically present in the
   dims array), for some examples:

\verbatim
    UnsqueezeR({3,4}, 0)  -> {1,3,4}
    UnsqueezeR({3,4}, 1)  -> {3,1,4}
    UnsqueezeR({3,4}, 2)  -> {3,4,1}
\endverbatim

     @param [in]    raxis   The index at which the extra axis is to appear.
                            We require 0 <= raxis <= p->num_axes.
     @param [in,out] p      The pattern to which we are adding an axis.
                            Will have its num_axes increased by 1
                            at exit, possibly its dims and strides
                            arrays changed, and its code updated.
 */
void UnsqueezeR(int32 raxis, TensorPattern *p);


/**
   Modifies 'p' in-place by inserting an axis with (dim=1,stride=0) at the
   specified axis-index (numbered in the public numbering).
   Equivalent to PyTorch's unsqueeze(), including its behavior with
   negative axis indexes (axis < 0 is interpreted as to num_axes + 1 - axis).

   Showing just the dims in the pattern, in the non-reversed order as
   exported by the API, some examples are:

\verbatim
    Unsqueeze([6,5], 0) -> [1,6,5]
    Unsqueeze([3,4], 1) -> [3,1,4]
    Unsqueeze([9,10], 2) -> [9,10,1]
    Unsqueeze([9,10], -1) -> [9,10,1]
\endverbatim

     @param [in]    axis   The index at which the extra axis is to appear.
                           We require -p->num_axes - 1 <= raxis <= p->num_axes
                           The large allowable range is because negative
                           axes are permitted, e.g. -1 means insert a new
                           axis after the last existing axis.
     @param [in,out] p      The pattern to which we are adding an axis.
                            Will have its num_axes increased by 1
                            at exit, possibly its dims and strides
                            arrays changed, and its code updated.
 */
inline void Unsqueeze(int32 axis, TensorPattern *p) {
  if (axis < 0) UnsqueezeR(1 - axis, p);
  else UnsqueezeR(p->num_axes - axis, p);
}

/**
   Modifies 'p' in-place by removing an axis with dim=1 from the specified
   position (in the reversed numbering physically used in the pattern).  Updates
   p->code.  It is an error if 'p' did not, on entry, contain an axis with dim=1
   as position 'raxis' in the array.


   Modifies 'p' in-place by removing an axis with dim=1 from the
   specified position specified in the reversed numbering physically used in the
   pattern.  Updates p->code.  It is an error if 'p' did not initially contain
   an axis with dim=1 at position 'raxis' in the array.

   This function updates p->code.

   In the example below we show the dims in the order they appear in the
   physical array:
\verbatim
   SqueezeR(0, {1,3,4})  -> {3,4}
   SqueezeR(1, {5,1,7})  -> {5,7}
   SqueezeR(2, {8,1,9})  -> [error]
\endverbatim
     @param [in]    raxis   The reversed-order axis to be squeezed.
                            We require 0 <= raxis < p->num_axes and
                            p->dims[raxis] == 1.
     @param [in,out] p      The pattern from which we are removing an
                            axis.  Will have its num_axes reduced by 1
                            at exit, possibly its dims and strides
                            arrays changed, and its 'code' updated.
*/
void SqueezeR(int32 raxis, TensorPattern *p);


/**
   Modifies 'p' in-place by removing an axis with dim=1 (hence stride=0)
   located at the specified axis (as numbered in the public numbering).
   Equivalent to PyTorch's squeeze(), including its behavior with
   negative axis indexes; axis < 0 is interpreted as to num_axes - axis,
   i.e. the last axis.  It is an error if 'p' did not, on entry,
   contain an axis with dim=1 at position 'axis' (in the public numbering).

   Showing just the dims in the pattern, in the non-reversed order as
   exported by the API, some examples are:
\verbatim
    Squeeze([1,6,5], 0) -> [6,5]
    Squeeze([3,1,4], 1) -> [3,4]
    Squeeze([9,1,10], 2) -> error
    Squeeze([7,1], -1) -> [7]
\endverbatim

     @param [in]    axis    The index at which the extra axis is to appear.
                            We require -p->num_axes <= axis < p->num_axes
                            (negative axes are permitted, interpreted
                            as an offset from p->num_axes).
                            We require that the specified axis have
                            dim=1.
     @param [in,out] p      The pattern from which we are removing an
                            axis.  Will have its num_axes reduced by 1
                            at exit, possibly its dims and strides
                            arrays changed, and its 'code' updated.
 */
inline void Squeeze(int32 axis, TensorPattern *p) {
  if (axis < 0) SqueezeR(1 - axis, p);
  else SqueezeR(p->num_axes - 1 - axis, p);
}


ybool Broadcastable(const TensorPattern &a, const TensorPattern &b,
                   bool b_non_reducing = false);


/**  This function returns true if the dimensions of tensor patterns
     a, b and c are broadcastable in the PyTorch sense (meaning;
     after padding their dims on the left with ones to make them
     have the same num-axes, corresponding dimensions are either
     identical or 1).  See the version of Broadcastable() above
     for more information.

       @param [in] a  The dimensions of the first Tensor
       @param [in] b  The dimensions of the second Tensor
       @param [in] c  The dimensions of the third Tensor
       @param [in] c_non_reducing   If true, then we do not allow a dim of
                      c to be 1 while corresponding dims of a or b
                      are > 1.
 */
bool Broadcastable(const TensorPattern &a, const TensorPattern &b,
                   const TensorPattern &c, bool c_non_reducing = false);



/**
   Returns true if the 'dims' vectors of a and b are the same.
   Does not require the number of axes to be the same, so effectively
   it's testing that the dims are the same after padding on the left
   with dim=1 (here referring to the public, non-reversed numbering
   of the dims).

   This is a stronger condition than Broadcastable(a, b).
 */
bool SameDim(const TensorPattern &a, const TensorPattern &b);


/**
   Returns true if the 'dims' vectors of a, b and c are all the same.
   Does not require the number of axes to be the same, so effectively
   it's testing that the dims are the same after padding on the left
   with dim=1 (here referring to the public, non-reversed numbering
   of the dims).

   This is a stronger condition than Broadcastable(a, b, c).
 */
bool SameDim(const TensorPattern &a, const TensorPattern &b,
             const TensorPattern &c);


/**
   Compresses a TensorPattern by removing or combining as many axes as possible.
   This version is suitable for operations that do not rely on any kind
   of structure, such as zeroing or nonlinearities; the only equivalence
   maintained is equivalence of the set of memory locations covered.
   The order of the (dim,stride) pairs in the input does not affect the
   output.  The output (dim,stride) pairs will be ordered from
   greatest to least stride (note: all output strides will be positive).

      @param [in,out]  pattern   The pattern to be compressed

      @param [out] data_offset  A number that we would have to add to
                          the data pointer of the source Tensor so
                          that 'dest' would cover the same set of
                          elements.  It will always be zero if 'src'
                          was free of negative strides.
   Examples are below, where we write a TensorPattern as

   `{{dim1,dim2,..}, {stride1,stride2,..}}`.

   (the curly braces in our notation imply that we are referring to the reversed
   ordering physically used in 'pattern', but actually this doesn't affect
   anything as the order of axes does not matter here as long as it is constent.

\verbatim
   Input pattern             Output pattern            Output offset
     {{10},{1}}               {{10},{1}}                  0
    {{3,4},{4,1}}             {{12},{1}}                  0
    {{4,3},{1,4}}             {{12},{1}}                  0
    {{9},{-1}}                {{9},{1}}                  -8
   {2,3,4},{100,4,1}        {{2,12},{100,1}}              0
\endverbatim
 */
void CompressOnePattern(TensorPattern *pattern,
                        int64 *data_offset);

/**
   Sorts the axes in 'pattern' from smallest to largest stride
   (in the reversed numbering physically present in 'pattern'; would
   be largest to smallest in the public API).  Useful in testing
   equivalence of patterns, as CompressOnePattern() followed
   by SortAxes() leads to a normalized form.

   This function requires that for 0 <= i < pattern->num_axes,
   pattern->strides[i] > 0.  This condition is satisfied by
   a pattern that has previously been compressed by CompressOnePattern().
   If in future we need to relax this constraint, we will do so.
   (The assumption of positive strides simplifies implementation
   because to normalize the form we'd have to make all strides
   positive, which would require outputting an offset).

     @param [in,out]  The pattern whose axes are to be sorted
                   from least to greatest stride (in the physical
                   ordering).
 */
void SortAxes(TensorPattern *pattern);


/*
  Compress two TensorPatterns by combining axes (and possibly
  flipping the sign of their strides and changing the data offset)
  The type of compression involved is the same as for CompressOnePattern
  (meaning we are doing some kind of operation that doesn't care about
  the structure, such as an element-by-element nonlinearity).

  The difference from calling CompressOnePattern() twice is that this function
  needs to preserve the relationship between the tensors whose pattern is src1
  and src2.  Suppose that a tensor with pattern src3 was the result of this
  elementwise operation satisfying Broadcastable(src1, src2, src3); there is
  only one such pattern.  Let x be a tuple which would be a valid index for the
  tensor with pattern src3.  Let us use an extended indexing convention
  whereby if an axis of src1 or src2 has dimension 1, we allow that axis to be
  indexed by any value, which would not affect the memory location because the
  stride is zero.  Then each such tuple x leads to a different pair of memory
  locations (p1, p2) in the tensors corresponding to patterns src1, src2.  The
  invariance that this function must preserve is that the set of memory-location
  pairs (p1, p2) must be the same in the output tensors (with their
  appropriately moved data pointers), as in the input tensors.

  What this means in practice is that we need to do the same operations on src1
  and src2.  For example, if flipping the sign of an axis of src1 we would have
  to flip that of src2, and if merging two axes of src1 we would have to merge
  the same two axes of src2.

    @param [in] src1  The first source pattern.
    @param [in] src2  The second source pattern.
                      We require Broadcastable(src1,src2) == true.
    @param [out] dest1  Compressed pattern out corresponding to src1.  Will
                     be free of negative strides (but dest2 might not be).
    @param [out] dest_offset1  Data offset that we'd need to add to src1's
                     data pointer before using the pattern 'dest1'
    @param [out] dest1  Compressed pattern out corresponding to src2.
                     Might not be free of negative strides if some dimensions
                     of src1/src2 had strides of opposite sign.
    @param [out] dest_offset1  Data offset that we'd need to add to src1's
                     data pointer before using the pattern 'dest1'


 */
void CompressTwoPatterns(TensorPattern *a,
                         TensorPattern *b,
                         int64 *data_offset_a,
                         int64 *data_offset_b);


/**
   Compresses one or more TensorPattern by removing or combining as many axes as
   possible.  See the documentation for CompressOnePattern() to understand the
   basic concept of compressing a single TensorPattern to a pattern with possibly
   fewer axes (and maybe with negative strides converted to positive),
   which covers the same set of memory locations as the original Tensor.

   The difference with just calling CompressOnePattern() several times is
   that CompressPatterns() preserves the relationships between the tensors.

   Firstly, we require that all pairs of TensorPattern in 'patterns' be
   broadcastable: that is, Broadcastable(p1, p2) would hold for any
   p1, p2 in 'patterns'.  In the explanation below we will use a
   'permissive indexing' convention whereby if a Tensor has an axis
   with dim,stride (0, 1), we allow it to be indexed by any value
   (not just zero), so that all the tensors represented can accept the
   same set of index tuples.  Suppose for example that there are three
   patterns, p1, p2, p3, in 'patterns', with 4 axes.  Let max_axes
   larger of the num-axes of p1, p2 or p3, and let
   x = (i, j, k, l) be an index tuple that would be valid for a tensor
   with that many axes.  Each such x, when used as an index into p1, p2
   and p3 with 'permissive indexing' as mentioned above, will
   give us a tuple of memory-offsets (o1, o2, o3); o1, o2 and o3 are indexes
   into the respective data pointers.  Ranging over the set of index-tuples
   x, we get a set of memory-offset tuples; call this set S_in,
   and call the set that we would get if doing the same procedure
   on the output tensors (with their possibly changed num-axes), be
   S_out.  Let us represent the 'data_offset' output of this function
   as (in this case) a 3-tuple o.  Then the invariant that this
   function needs to satisfy is that:

        `S_in = S_out + o`

   (this equates two sets of 3-tuples, in our example) where we interpret the '+
   o' as adding to each element of the set.  The '+ o' above would only be
   necessary if any strides were negated; it is a tuple containing offsets, in
   elements, to be added to the data pointers of the respective output tensors.


      @param [in,out] patterns   An nonempty array of the patterns
                         to be jointly compressed.
      @param [out]  data_offsets  Pointer to an array of the same size
                        as 'patterns', which on output will contain
                        offsets to be added to the data pointers.

      @return  Returns true if it made any change to the patterns,
               false if they were unchanged.  If false, the
               data_offsets will be set to zero.

 Examples are below, where we write a TensorPattern as
 `{{dim1,dim2,..}, {stride1,stride2,..}}`.

\verbatim
    src1                src2              dest1,offset1       dest2,offset2
  {{10},{1}}           {{10},{1}}        {{10},{1}},0        {{10},{1}},0  # no-op
  {{8},{1}}            {{1},{0}}         {{8},{1}},0         {{1},{0}},0   # no-op
  {{7},{-1}}           {{7},{1}}         {{7},{1}},-6         {{7},{-1}},6 # flip sign
 {{3,4},{4,1}}        {{3,4},{4,1}}      {{12},{1}},0         {{12},{1}},0 # combine dims
 {{3,4},{4,1}}        {{3,1},{4,0}}      {{3,4},{4,1}}        {{3,1},{4,0}} # can't combine, would be incompatible
 {{3,4},{4,1}}        {{1,1},{0,0}}      {{12},{1}}           {{1},{0}}    # combine
\endverbatim
 */
bool CompressPatterns(ArrayRef<TensorPattern*> patterns,
                      int64 *data_offsets);

/**
   Compresses a TensorPattern by removing or combining as many axes as possible,
   while respecting certain invariances that are relevant when constructing
   'views' ('view' is PyTorch terminology; the NumPy equivalent is 'reshape').
   The "C" in the function name refers to C-style arrays.

    This function removes axes with dim=1.

   This function combines successive axes if the relationship of their
   dims and strides is what you would expect in a "C"-style array
   when the axes are listed in their non-reversed ordering (i.e.
   as exposed by class Tensor).


   Suppose that in pattern 'p' we had two successive axes physically numbered
   raxis, raxis+1, with p->dims[raxis] > 1 and p->dims[raxis+1] > 1
   and p->strides[raxis + 1] == p->strides[raxis] * p->dims[raxis],
   then this function will merge them into a single axis with dimension
   the product of the two dimensions..

    TODO...

   finish this if it turns out to be needed for something.


   with dims and
   strides (dim_a, dim_b) and (stride_a, stride_b), with dim_a > 1 and
   dim_b > 1.  If stride_a == stride_b * dim_b, then this function
   will merge them into a single axis with dimension (dim_a * dim_b)
   and stride stride_b.   (However, they won't be merged if it would
   result in a dimension exceeding the range of int32).

   The output pattern 'dest' is what you get if you keep applying the
   rules above until no further change is made.

   Examples are below, where we write a TensorPattern as
  `   {{dim1,dim2,..}, {stride1,stride2,..}}`.
\verbatim
   Input pattern             Output pattern
     {{10},{1}}               {{10},{1}}
    {{5,1},{1,1}}             {{5},{1}}
    {{9},{-1}}                {{9},{-1}}
   {2,3,4},{100,4,1}        {{2,12},{100,1}}
   {2,3,4},{100,-4,-1}        {{2,12},{100,-1}}
\endverbatim
 */
void CompressPatternC(TensorPattern *p);



/**
   Creates a TensorPattern corresponding to a requested 'view' of the matrix.
   ('view' is PyTorch terminology; the NumPy equivalent is 'reshape').

   The PyTorch/NumPy semantics are (I believe) as follows: Firstly, a view
   can/should only be created for a tensor whose layout in memory is as for a
   "C" array; suppose that the shape of array a is (9, 8), a "C" layout would
   imply strides of (8, 1).  A 'view' of this array simply implies interpreting
   the same block of memory as a "C" array with some other sequence of
   dimensions, say (3, 3, 8) or (8, 9) or (1, 72); any sequence whose product
   matches the number of elements in "a".

   Our semantics of "view" is the same as that of PyTorch/NumPy except that we
   impose fewer constraints on what strides the input Tensor cmay have.  Let the
   'view' of the array 'a' be 'b'.  As long as it is possible to find a tensor
   pattern for 'b' that would lead to the same relationship between the elements
   of 'a' and 'b' as what you would get by asking for the same "view" in
   PyTorch/NumPy assuming 'a' had had "C"-style strides (viewed in terms of
   indexed elements of and b, without regard to the physical memory layout), we
   allow it.


   Notes on implementation (glossing over ones in 'dims' which are easy to
   handle as a special case): we would first call CompressPattern on
   'pattern_in'.  Then we would attempt to find a correspondence with
   the dimensions of this compressed pattern and a partition of the
   sequence 'dims'.  For example, suppose the compressed pattern
   is (100, 9) and dims is (50, 2, 3, 3), then the partition would
   be (50, 2), (3, 3).  If this is not possible (e.g. if dims
   had been (30,10,3) instead), we return false.

   @param [in]  pattern_in   The input pattern for which we are trying to
                          find an alternative view
   @param [in]  dims  The sequence of dimensions corresponding to the
                      desired view.  Its product must be the same as the
                      product of pattern_in.dims.
   @param [out] pattern_out  The output pattern, if we were
                      successful (otherwise undefined).  Its 'dims'
                      will be the same as 'dims'.
   @return           Returns true on success (i.e. such a view existed),
                     and false otherwise.  This function will never return
                     false if 'pattern_in' had strides as for a "C" array
                     (i.e., if its properties' has_c_strides was true).

 */
bool CreateViewPattern(const TensorPattern &pattern_in,
                       ArrayRef<int32> dims,
                       TensorPattern *pattern_out);



}  // namespace tensor
}  // namespace kaldi

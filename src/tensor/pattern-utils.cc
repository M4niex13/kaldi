// tensor/pattern-utils.cc

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

#include "tensor/pattern-utils.h"

namespace kaldi {
namespace tensor {

int32 ComputePatternCode(const Pattern &pattern) {
  int32 ans = 0;

  int32 n = 0;
  // n is going to be:
  // n = 0 if no axis had stride=1, otherwise:
  // n = 1 + the raxis index that had stride=1.

  bool found_negative_dim = false;

  // caution: this axis index is a shifted-to-the-right index,
  // not the one that the public interface of Tensor exposes.
  for (int32 raxis = 0; raxis < pattern.num_axes; raxis++) {
    int32 dim = pattern.dims[raxis],
        stride = pattern.strides[raxis];
    if (dim != 1) {
      ans |= 1;  // set least significant bit of 'ans' to 1.
      if (dim < 0)
        found_negative_dim = true;
      if (stride == 1)
        n = raxis + 1;  // Can happen only once, if pattern.Check() == true,
                        // i.e. if pattern is valid.
    }
    ans <<= 1;  // shift left by one.
  }

  // add in the value 'n' shifted 8 bits to the left,
  // and set the 11th bit if we found a negative dim.
  ans |= (n << 8) |  (found_negative_dim ? 1 << 11 : 0);
}


void ComputeMinAndMaxMindex(const Pattern *pattern,
                            int64 *min_mindex,
                            int64 *max_mindex) {
  KALDI_PARANOID_ASSERT(IsValid(pattern));
  int32 num_axes = pattern.num_axes;
  if (ContainsNegativeStride(pattern.code)) {
    // The if-statement above may be read as "if either pattern.code is -1 or it
    // indicates that `pattern` contains a negative stride.  That is, at this
    // point all we know is that `pattern` *might* contain a negative stride.
    int64 min_mindex_sum = 0, max_mindex_sum = 0;
    for (int32 raxis = 0; raxis < num_axes; raxis++) {
      int64 prod (pattern.dims[raxis] - 1) *
          static_cast<int64>(pattern.strides[raxis]);
      if (pattern.strides[raxis] > 0) max_mindex_sum += prod;
      else min_mindex_sum += prod;
    }
    *min_mindex = min_mindex_sum;
    *max_mindex = max_mindex_sum;
  } else {
    // This is a faster branch of the code where we know that all strides are
    // nonnegative.
    *min_mindex = 0;
    int64 max_mindex_sum = 0;
    for (int32 raxis = 0; raxis < num_axes; raxis++)
      max_mindex_sum += (pattern.dims[raxis] - 1) *
          static_cast<int64>(pattern.strides[raxis]);
    *max_mindex = max_mindex_sum;
  }
}



/**
   This utility function used in CompressPatterns() normalizes the signs of the
   strides in all the dimensions, prior to any merging of axes, and sets the
   'data_offsets' variables.

   Consider an axis-index i (i.e. an index into the patterns' dims or strides
   vector).  We say that the strides for axis i
   are normalized if either all patterns have zero stride for that axis
   or the lowest-numbered pattern which has nonzero stride for that axis
   has positive stride for that axis.

   This type of normalization is done to increase the chance that we can combine
   axes, because the rule we use for combining axes only applies if any nonzero
   strides present have the same sign between the two axes.  In terms of being
   able to combine the maximum number of axes this rule is optimal, because any
   two axes where the pattern-index of the first pattern with a nonzero stride
   for those axes is different, would *not* be combinable.  So for any pair of
   axes that are potentially combinable according to that criterion and which
   have any nonzero strides, our normalization rule ensures that at least one
   pair of nonzero strides has the same sign.  If there were another pattern for
   which the sign was opposite after applying our rule, those two axes would not
   be combinable whatever the sign normalization.

     @param [in,out] patterns  The patterns to have their strides normalized
     @param [in]    max_num_axes  The maximum of any of the patterns'
                          num_axes (provided so we don't have to work it
                          out from 'patterns').
     @param [in,out] data_offsets  Data offsets, an array of dimension
                          patterns.size, which will be *added to* as needed by
                          this function, by the amount required to ensure that
                          the memory locations visited by the set of possible
                          indexes into these patterns is the same before and
                          after any change of sign.
     @return   Returns true if it made a change, else false.

   CAUTION!  Does not update the pattern code (the code for that is commented).
   If this were moved to a header we would have to make it update the pattern
   code.
 */
static inline bool NormalizeSigns(ArrayRef<Pattern*> patterns,
                                  int32 max_num_axes,
                                  int64 *data_offsets) {
  bool changed = false;
  size_t num_patterns = patterns.size;

  for (int32 a = 0; a < max_num_axes; a++) {
    for (size_t p = 0; p < size; p++) {
      if (patterns[p]->strides[a] != 0) {
        // We have identified the first pattern-index with nonzero
        // stride for this axis
        if (patterns[p]->strides[a] < 0) {
          changed = true;
          // The stride is negative, so we have to flip it for this axis.
          // (Note: we flip it for all patterns, but we can ignore
          // pattern-indexes q < p because we know all those strides are zero.
          for (size_t q = p; q < size; q++) {
            // cast to int64 before muiltiplication to avoid potential
            // overflow
            if (patterns[q]->strides[a] != 0) {
              int64 this_offset =
                  static_cast<int64>(patterns[q]->dims[a] - 1) *
                  static_cast<int64>(patterns[q]->strides[a]);
              data_offsets[q] += this_offset;
              patterns[q]->strides[a] *= -1;
              // patterns[q]->code = -1;  // A signal to recompute the code.
            }
          }
        }
        // break from loop over patterns; we identified the first pattern-index
        // with nonzero stride for this axis, which is the only thing that
        // determines whether we change the sign of this axis.
        break;
      }
    }
  }
  //if(changed)
  //  for (size_t p = 0; p < size; p++)
  //    if (patterns[p]->code == -1)
  //      patterns[p]->code == GetDimsCode(*(patterns[p]));
  return changed;
}


/**
   This is a note on the semantics of combining dimensions in CompressPatterns.
   It is not a commutative property: Combinable(pattern, i, j) might not
   equal Combinable(pattern, j, i).

   We can only ever combine pairs of axes that were combinable for *all* patterns
   passed to CompressPatterns().

   Two axes are combinable if stride2 == stride1 * dim1.  Here, raxis1 is
   required to be the axis with the smaller stride, which is the asymmetry
   between them.

   (We also require that the new dimension must not overflow an int32.)
 */
static inline bool Combinable(const Pattern &p,
                              int32 raxis1, int32 raxis2) {
  return pattern.strides[raxis2] == p.strides[raxis1] * p.dims[raxis1] &&
      static_cast<int64>(p.dims[raxis1])*static_cast<int64>(p.dims[raxis2]) <
    std::numeric_limits<int32>::max();
}


// Returns true iff the axis 'axis' has zero stride (and hence dim=1)
// for all the supplied patterns.  An axis like this can be removed without
// affecting the result.
static inline bool AxisIsTrivial(ArrayRef<Pattern> patterns,
                                 int32 raxis) {
  for (size_t p = 0; p < patterns.size; p++)
    if (patterns[p].strides[raxis] != 0)
      return false;
  return true;
}

// Combine the two axes raxis1 and raxis2 in all the patterns (which the user
// asserts is possible); at exit, the higher numbered of the two raxes is
// guaranteed to have dim=1, stride=0 in all patterns.  (we will later get rid
// of that trivial axis).  axis1 is the one with the smaller stride, and is the
// one whose stride we keep in the combined axis; that is the asymmetry
// between axis1 and axis2.
static inline void CombineAxes(ArrayRef<Pattern*> patterns,
                               int32 raxis1, int32 raxis2) {
  size_t num_patterns = patterns.size;
#ifdef KALDI_PARANOID
  for (size_t p = 0; p < num_patterns; p++) {
    KALDI_ASSERT(Combinable(*(patterns[p]), raxis1, raxis2));
  }
#endif
  if (raxis1 > raxis2) {
    // keep raxis2, remove raxis1.
    // We want the 'trivial' axis (the one with dim=1, stride=0 for all
    // patterns) to be the higher-numbered axis (this helps reduce
    // the chance of having to move dims/strides around when removing
    // trivial axes later on.
    for (size_t p = 0; p < num_patterns; p++) {
      Pattern *pattern = patterns[p];
      pattern->dims[raxis2] *= pattern->dims[raxis1];
      pattern->strides[raxis2] *= pattern->strides[raxis1];
      pattern->dims[raxis1] = 1;
      pattern->strides[raxis1] = 0;
    }
  } else {
    // keep raxis1, remove raxis2.
    for (size_t p = 0; p < num_patterns; p++) {
      Pattern *pattern = patterns[p];
      pattern->dims[raxis1] *= pattern->dims[raxis1];
      pattern->dims[raxis2] = 1;
      pattern->strides[raxis2] = 0;
    }
  }
}

/**
   Removes trivial axes, defined as axes for which, for all patterns, dim=1 and
   stride=0.  Assumes the user has already found out which axes are trivial and
   is passing in this information as the array 'trivial_raxis' (we include the r
   to emphasize that we use the same reversed numbering as in
   pattern.{dims,strides}).


   This function removes those axes, shifts the dims and strides arrays to
   the left as needed, and decreases the 'num_axes' of the patterns
   appropriately (note: this is not as simple as just subtracting the number
   of axes removed, because removing an raxis that was >= the num_axes
   of a given pattern needs to be a no-op).

   @param [in]  trivial_raxis    An array which identifies the axes to
                       be removed.  At least one element must be true.
                       Indexed by 'raxis'.
   @param [in,out]  patterns    The patterns to be modified.

   CAUTION: this function does not update the codes of 'patterns'.
 */
static void RemoveTrivialAxes(bool is_trivial_raxis[KALDI_TENSOR_MAX_AXES],
                              ArrayRef<Pattern*> patterns) {
  int32 first_trivial_raxis = -1;
  for (int32 raxis = 0; raxis < KALDI_TENSOR_MAX_AXES; raxis++) {
    if (is_trivial_axis[raxis]) {
      first_trivial_raxis = raxis;
      break;
    }
  }
  KALDI_PARANOID_ASSERT(first_trivial_raxis >= 0);

  for (size_t p = 0; p < patterns.size; p++) {
    Pattern *pattern = patterns[p];
    // Keep the axes right-justified.  We work from the right to the left.

    // We do the loop over axes inside the loop over p for memory locality.
    // We keep the axes shifted to the right so the loop goes backwards.
    int32 raxis_out = first_trivial_raxis,
        num_axes = pattern->num_axes;
    for (int32 raxis_in = raxis_out; raxis_in < num_axes; raxis_in++) {
      if (is_trivial_axis[raxis_in]) {
        KALDI_PARANOID_ASSERT(pattern->dims[raxis_in] == 1);
      } else {
        if (raxis_out != raxis_in) {
          pattern->dims[raxis_out] = pattern->dims[raxis_in];
          pattern->strides[raxis_out] = pattern->strides[raxis_in];
        }
        raxis_out++;
      }
    }
    pattern->num_axes = raxis_out;
    // Make sure the axes we removed are set to dim=1, stride=0.
    for (; raxis_out < num_axes; raxis_out++) {
      pattern->dims[raxis_out] = 1;
      pattern->strides[raxis_out] = 0;
    }
    KALDI_PARANOID_ASSERT(pattern->Check(false));
  }
}

void CompressPatterns(ArrayRef<Pattern*> patterns,
                      int64_t *data_offsets) {
  size_t num_patterns = patterns.size;
#ifdef KALDI_PARANOID
  KALDI_ASSERT(num_patterns > 0);
  for (size_t p = 0; p < num_patterns; p++) {
    KALDI_ASSERT(patterns[p]->Check());
    for (size_t q = p + 1; q < num_patterns; q++) {
      KALDI_ASSERT(Broadcastable(*(patterns[p]), *(patterns[q])));
    }
  }
#endif
  for (size_t p = 0; p < num_patterns; p++)
    data_offsets[p] = 0;

  int32 max_num_axes = patterns[0]->num_axes,
      combined_code = patterns[0]->code;
  // combined_code is the '|' of the patterns' codes; it's
  // not the same as what CombineCodes() would return.

  for (size_t p = 1; p < num_patterns; p++) {
    max_num_axes = std::max<int32>(max_num_axes, patterns[p]->num-axes);
    combined_code |= patterns[p]->code;
  }
  bool changed = false;
  if (ContainsNegativeStride(combined_code))
    changed = NormalizeSigns(patterns, data_offsets);

  // note: the codes won't be fully up to date at this point.

  bool exists_trivial_axis = false;
  // The = {} ensures (I believe) that they are all set to 0, meaning false.
  bool is_trivial_raxis[KALDI_TENSOR_MAX_AXES] = {};
  for (int32 raxis = 0, mask = 1; raxis < max_num_axes; raxis++, mask <<= 1) {
    if ((combined_code | mask) == 0) {
      is_trivial_raxis[raxis] = true;
      exists_trivial_axis = true;
    }
  }

  // The reason we go in reverse order is a small optimization; it
  // means it's more straightforward, when combining, to 'make trivial'
  // the higher-numbered raxis, which reduces the chances of having to
  // copy axes to different positions later on to remove trivial axes.
  // (If we went forward and did this, we'd have to repeat processing
  // the current axis each time we combined, which would be a hassle).
  for (int32 raxis1 = max_num_axes - 1; raxis1 >= 0; raxis1--) {
    if (is_trivial_raxis[raxis1])
      continue;

    // see if axis i can be combined (in either direction) with any
    // earlier-numbered axis.
    for (int32 raxis2 = raxis1 - 1; raxis2 >= 0; raxis2--) {
      if (is_trivial_raxis[raxis2])
        continue;
      bool combinable_12 = true;
      for (size_t p = 0; p < num_patterns; p++) {
        if (!Combinable(patterns[p], raxis1, raxis2)) {
          combinable_12 = false;
          break;
        }
      }
      if (combinable_12) {
        CombineAxes(patterns, raxis1, raxis2);
        is_trivial_raxis[raxis1] = true;  // higher numbered raxis is removed.
        exists_trivial_axis = true;
        // Break from the loop over raxis2 and continue over the loop over
        // raxis1, meaning we are done combining with axis 'raxis1' (it's
        // trivial now).
        break;
      }
      bool combinable_21 = true;
      for (size_t p = 0; p < num_patterns; p++) {
        if (!Combinable(patterns[p], raxis2, raxis1)) {
          combinable_21 = false;
          break;
        }
      }
      if (combinable_21) {
        CombineAxes(patterns, raxis2, raxis1);
        is_trivial_raxis[raxis1] = true;  // higher numbered raxis is removed.
        exists_trivial_axis = true;
        break;
      }
    }
  }
  if (exists_trivial_axis) {
    RemoveTrivialAxes(max_num_axes, is_trivial_raxis, patterns);
    changed = true;
  }
  if (changed)
    for (size_t p = 0; p < num_patterns; p++)
      patterns[p]->code = ComputePatternCode(*(patterns[p]));
  return changed;
}


void CompressOnePattern(Pattern *pattern,
                        int64 *data_offset) {
  // We may at some point implement this specially; doing this would be more efficient.
  CompressPatterns({pattern}, data_offset);
}


void SortAxes(Pattern *pattern) {
  int32 num_axes = pattern->num_axes;
  switch(num_axes) {
    case 0: case 1:
      return;
    case 2:
      // Implement this as a special case, avoiding a temporary
      if (pattern->strides[0] > pattern->strides[1] ||
          (pattern->strides[0] == pattern->strides[1] &&
           pattern->dims[0] > pattern->dims[1])) {
        std::swap(pattern->strides[0], pattern->strides[1]);
        std::swap(pattern->dims[0], pattern->dims[1]);
      }
      pattern->code = -1;
      return;
    default: {
      std::pair<int32,int32> dims_strides[KALDI_TENSOR_MAX_DIM];
      for (int32 i = 0; i < num_axes; i++) {
        dims_strides[i].first = pattern->dims[i];
        dims_strides[i].second = pattern->strides[i];
      }
      std::sort(dims_strides, dims_strides + num_axes,
                // below is a C++11 lambda used as a comparator function, like
                // the operator a < b.
                [] (const std::pair<int32,int32> &a,
                    const std::pair<int32,int32> &b) {
                  int32 abs_stride_a = std::abs(a.second),
                      abs_stride_b = std::abs(b.second);
                  if (abs_stride_a < abs_stride_b) return true;  // a < b; sort on abs(stride) first.
                  else if (abs_stride_a > abs_stride_b) return false;  // a > b
                  else return (a.first < b.first);
                  // sort on dim if strides are the same
                  // (which should only be for stride=0 for any valid Pattern.
                });
      for (int32 i = 0; i < num_axes; i++) {
        pattern->dims[i] = dims_strides[i].first;
        pattern->strides[i] = dims_strides[i].second;
      }
      pattern->code = -1;
      return;
    }
  }
}

void SortTupleAxes(ArrayRef<Pattern*> patterns) {

}

void Transpose(int32 raxis1, int32 raxis2, Pattern *p) {
  if (static_cast<uint32>(raxis1) >= static_cast<uint32>(p->num_axes) ||
      static_cast<uint32>(raxis2) >= static_cast<uint32>(p->num_axes)) {
    KALDI_ERR << "Invalid axes to transpose: raxis1="
              << raxis1 << ", raxis2=" << raxis2
              << ", num-axes = " << p->num_axes;
  }
  std::swap(p->strides[raxis1], p->strides[raxis2]);
  std::swap(p->dims[raxis1], p->dims[raxis2]);
  p->code = -1;
}

void Transpose(int32 axis1, int32 axis2, Pattern *p) {
  int32 num_axes = p->num_axes;
  // interpret negative axes as offsets from num_axes.

  // Work out the reversed / private axis indexes that we physically use
  // in the arrays.  This includes interpreting negative axis
  // indexes as being relative to the number of axes.
  int32 raxis1 = (axis1 < 0 ? axis1 + 1 : num_axes - 1 - axis1),
      raxis2 = (axis2 < 0 ? axis2 + 1 : num_axes - 1 - axis2);
  if (static_cast<uint32>(raxis1) >= static_cast<uint32>(p->num_axes) ||
      static_cast<uint32>(raxis2) >= static_cast<uint32>(p->num_axes)) {
    KALDI_ERR << "Invalid axes to transpose: axis1="
              << axis1 << ", axis2=" << axis2 << ", num-axes = " << p->num_axes;
  }
  std::swap(p->strides[raxis1], p->strides[raxis2]);
  std::swap(p->dims[raxis1], p->dims[raxis2]);
  p->code = -1;
}



void RemoveTrivialAxes(Pattern *pattern) {
  int32 num_axes = pattern->num_axes,
      num_axes_out = 0;
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    int32 this_dim = pattern->dims[raxis];
    if (this_dim != 1) {
      if (num_axes_out != raxis) {
        pattern->dims[num_axes_out] = this_dim;
        pattern->strides[num_axes_out] = pattern->strides[raxis];
      }
    }
  }
  // It is a requirement of struct Pattern that dims and
  // strides for raxis >= num_axes be 1 and 0 respectively.
  for (int32 raxis = num_axes_out; raxis < num_axes; raxis++) {
    pattern->dims[raxis] = 1;
    pattern->strides[raxis] = 0;
  }
  pattern->num_axes = num_axes;
  pattern->code = -1;
}


void RemoveTrivialAxes(const Pattern &pattern_in,
                       Pattern *pattern_out) {
  KALDI_PARANOID_ASSERT(pattern_out != &pattern_in);
  int32 num_axes = pattern->num_axes,
      num_axes_out = 0;
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    int32 this_dim = pattern_in.dims[raxis];
    if (this_dim != 1) {
      pattern_out->dims[num_axes_out] = this_dim;
      pattern_out->axes[num_axes_out] = pattern_in.strides[raxis];
    }
  }
  // It is a requirement of struct Pattern that dims and
  // strides for raxis >= num_axes be 1 and 0 respectively.
  for (int32 raxis = num_axes_out;
       raxis < KALDI_TENSOR_MAX_AXES; raxis++) {
    pattern_out->dims[raxis] = 1;
    pattern_out->strides[raxis] = 0;
  }
  pattern_out->num_axes = num_axes_out;
  pattern_out->code = -1;
}

int64 NumElements(const Pattern &pattern) {
  int32 num_axes = pattern.num_axes;
  int64 ans = 1;
  for (int32 raxis = 0; raxis < num_axes; raxis++)
    ans *= pattern.dims[raxis];
  return ans;
}

void Select(int32 eaxis, int32 index,
            const Pattern &src, Pattern *dest) {
  KALDI_PARANOID_ASSERT(src.IsValid());
  int32 num_axes = src.num_axes,
      raxis = EaxisToRaxis(eaxis);
  if (static_cast<uint32>(raxis) >= static_cast<uint32>(num_axes) ||
      static_cast<uint32>(index) >= static_cast<uint32>(src.dims[axis])) {
    // If raxis is not in the range [0, num_axes - 1] or the index
    // is not in the range [0, src.dims[axis] - 1]...
    KALDI_ERR << "Invalid args to Select(): axis=" << eaxis
              << " index=" << index << " vs. pattern dims="
              << DimsAsString(src);
  }
  dest->num_axes = src.num_axes - 1
  for (int32 r = 0; r < raxis; r++) {
    dest->dims[r] = src.dims[r];
    dest->strides[r] = src.strides[r];
  }
  dest->offset = src.offset + index * src.strides[raxis];
  for (int32 r = raxis + 1; r < num_axes; r++) {
    dest->dims[r - 1] = src.dims[r];
    dest->strides[r - 1] = src.strides[r];
  }
  for (int32 r = num_axes - 1; r < KALDI_TENSOR_MAX_DIM; r++) {
    dest->dims[r] = 1;
    dest->strides[r] = 0;
  }
  dest->code = -1;
  dest->properties = 0;
}

void Slice(int32 axis, int32 start, int32 end, Pattern *pattern) {
  int32 num_axes = pattern->num_axes,
      raxis = EaxisToRaxis(eaxis);
  KALDI_PARANOID_ASSERT(pattern->IsValid());
  if (static_cast<uint32>(raxis) >= static_cast<uint32>(num_axes) ||
      end <= start ||
      static_cast<uint32>(end) >= static_cast<uint32>(src.dims[axis])) {
    // If raxis is not in the range [0, num_axes - 1] or (end <= start)
    // or end is not in the range [0, src.dims[axis] - 1]...
    KALDI_ERR << "Invalid args to Slice(): axis=" << eaxis
              << " start=" << start << " end=" << end << " vs. pattern dims="
              << DimsAsString(*pattern);
  }
  int32 old_stride = pattern->strides[raxis];
  pattern->offset += pattern->strides[raxis] * start;
  int32 new_dim = end - start;
  pattern->dims[raxis] = new_dim;
  if (new_dim == 1) {
    pattern->strides[raxis] == 0;
    if (pattern->code >= 0) {
      // If the code was set, the following keeps it up to date (it's faster
      // then recomputing the whole thing.

      // Make the bit in the code that says the dim was != 1, is not set.
      pattern->code &= ~(1 << (raxis - 1));
      if (old_stride == 1) {
        // If the stride was 1 then the code would have had at least one of bits
        // 8,9,10 set to indicate the value of 'raxis'.  Zero this out, since
        // the stride is no longer 1.
        pattern->code &= ~(0x700);
      }
    }
  }
  KALDI_PARANOID_ASSERT(pattern->IsValid());
}


void UnsqueezeR(int32 raxis, const Pattern &src, Pattern *dest) {
  int32 num_axes_in = src.num_axes;
  KALDI_ASSERT(static_cast<uint32>(raxis) <= num_axes_in &&
               num_axes_in < KALDI_TENSOR_MAX_DIM);
  KALDI_PARANOID_ASSERT(IsValid(src));
  if (&src != dest) {
    // Copy some things over
    for (int32 r = 0; r < raxis; r++) {
      dest->dims[r] = src.dims[r];
      dest->strides[r] = src.strides[r];
    }
    for (int32 r = num_axes_in + 1; r < KALDI_TENSOR_MAX_DIM; r++) {
      dest->dims[r] = 1;
      dest->strides[r] = 0;
    }
  }
  dest->num_axes = num_axes_in + 1;
  for (int32 r = num_axes_in + 1; r > raxis; r--) {
    // go in reverse order in case (&src == dest)
    dest->dims[r] = src.dims[r - 1];
    dest->strides[r] = src.strides[r - 1];
  }
  // The unsqueezed axis.
  dest->dims[raxis] = 1;
  dest->strides[raxis] = 0;
  if (raxis != num_axes_in) {
    dest->code = -1;
    dest->properties = 0;
  } else if (&src != dest) {
    // code would be unaffected if raxis == num_axes_in.
    dest->code = src.code;
    dest->properties = src.properties;
  }
  KALDI_PARANOID_ASSERT(dest->IsValid());
}

}  // namespace kaldi
}  // namespace tensor

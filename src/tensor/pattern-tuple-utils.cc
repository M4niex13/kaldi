// tensor/pattern-extra-utils.cc

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


#include "tensor/pattern-extra-utils.h"

namespace kaldi {
namespace tensor {




/**
   This function, not declared in the header, creates a sorted list of all the
   stride values which are present in either 'pattern1' or 'pattern2'.  These
   will all be positive, since pattern1 and pattern2 are required to be in
   canonical form.

     @param [in] pattern1   First input pattern, must be in canonical form.
     @param [in] pattern2   Second input pattern, must be in canonical form.
     @param [out] strides   A sorted list of all stride values that are present
                            in either pattern1 or pattern2 will be written
                            to here.  There will be no repeats.
*/
static void FindAllStrides(
    const Pattern &pattern1,
    const Pattern &pattern2,
    std::vector<int32> *strides) {
  KALDI_PARANOID_ASSERT(IsCanonical(pattern1) && IsCanonical(pattern2));
  strides->clear();
  strides->reserve(pattern1_.num_axes + pattern2_.num_axes);
  for (int32 raxis = 0; raxis < pattern1.num_axes; raxis++)
    strides->push_back(pattern1.strides[raxis]);
  for (int32 raxis = 0; raxis < pattern2.num_axes; raxis++)
    strides->push_back(pattern2_.strides[raxis]);
  SortAndUniq(strides);  // sort from least to greatest; remove duplicates.
}



// See declaration in header.
bool IsRegular(const Pattern &pattern) {
  int32 num_axes = pattern.num_axes;

  for (int32 i = 0; i + 1 < num_axes; i++) {
    int32 this_stride = pattern.strides[i],
        this_dim = pattern.dims[i],
        this_prod = this_stride * this_dim;
    for (int32 j = i + 1; j < num_axes; j++) {
      if (pattern.strides[j] >= this_prod) {
        // in this case, 'j' would be the 'k' value used in the proof.  If we
        // fall off this loop, it would correspond to k == num_axes, which is
        // also OK.
        break;
      } else if (pattern.dims[j] != 1 ||
                 pattern.strides[j] % this_stride != 0) {
        return false;
      }
    }
  }
  return true;
}


/**
   This function, called by ConvertPatternStrides(), is not declared in the
   header.  It converts a pattern in canonical form to a Pattern whose strides
   are equal to the provided 'strides' vector, which is valid-2,
   and has normalized (i.e. positive and increasing) strides.


       @param [in] pattern_in  The input pattern; must be valid and
                               in canonical form.
       @param [in] strides     The list of strides which we want
                               'pattern_out' to have.  Must be a list of
                               positive integers sorted from least to
                               greatest with size <= KALDI_TENSOR_MAX_AXES,
                               and all strides in pattern_in must
                               be present in this list.
       @param [out] pattern_out  The output pattern (must not point to
                               pattern_in).  On exit its memory-index-set will
                               equal that of pattern_in; its strides will be
                               equal to 'strides' (including the order, when
                               numbered in the private numbering); it will
                               be valid-2, and it will be linear in pattern_in.
*/
static void ConvertPatternStridesLazily(
    const Pattern &pattern_in,
    const std::vector<int32> &strides,
    Pattern* pattern_out) {
  KALDI_PARANOID_ASSERT(IsCanonical(pattern_in));
  int32 num_axes_in = pattern_in.num_axes,
      num_axes_out = strides.size();
  pattern_out->num_axes = num_axes_out;
  pattern_out->code = -1;
  int32 raxis_in = 0;
  pattern_out->offset = pattern_in->offset;
  // The following code relies on pattern_in being in canonical form
  // (so its strides are in sorted order), and all of its strides being
  // present in the list 'strides'.
  for (int32 raxis_out = 0; raxis_out < num_axes_out; raxis_out++) {
    int32 stride = strides[raxis_out];
    pattern_out->strides[raxis_out] = stride;
    if (pattern_in.strides[raxis_in] == stride) {
      pattern_out->dims[raxis_out] = pattern_in.dims[raxis_in];
      pattern_in++;
    } else {
      pattern_out->dims[raxis_out] = 1;
    }
  }
  if (raxis_in != num_axes_in) {
    KALDI_ERR << "Something went wrong converting strides; trying to "
        "convert pattern with strides = " << StridesAsString(pattern_in)
              << " to strides " << ArrayAsString(strides);
  }
}



/**
   This function, not declared in the header, attempts to ensure that the axis-sorting
   property in a provided Pattern holds for the axis-index 'raxis' (in the private
   numbering, of course).  I.e. it ensures (for the pattern we are to modify) that:

      `pattern->strides[raxis+1] >= pattern->strides[raxis] * pattern->dims[raxis]`.

   This function expects that the pattern will also satisfy that property for
   all axis-indexes `0 <= i < raxis`, and will be valid-2.  This function will
   always succeed if the pattern is regular (see IsRegular(), and "Regularity
   property" in the glossary).

   Ensuring this property exists may sometimes require splitting this Pattern up
   (i.e. adding extra Patterns); the union of their memory-index-sets together
   with that of the modified pattern will equal the memory-index-set of the
   original pattern at input (these sets being unioned will be disjoint).  Any
   newly created Patterns will be appended to the vector 'patterns'.

    @param [in]      raxis    The axis for which we are ensuring that the
                             axis-sorting property holds.
    @param [in]      pattern_index  The index in the vector 'patterns'
                             of the pattern for which we are ensuring that
                             the axis-sorting property holds.
    @param [in,out]  patterns  The vector of patterns in which to look for the
                             pattern to operate on; we may also append
                             Patterns to this vector if needed, as mentioned
                             above.  Note: the newly added patterns may not satisfy
                             the axis-sorting property for 'raxis', but they will
                             still satisfy it for all axes numbered less than
                             'raxis', assuming the pattern at 'pattern_index'
                             did at entry.

    @return                  Returns true on success, false on failure.
                             Will always return true if `(*patterns)[pattern_index]`,
                             satisfied the 'regularity property' at entry;
                             see IsRegular().
 */
static bool EnsureAxisSortingPropertyHolds(
    int32 raxis,
    int32 pattern_index,
    std::vector<Pattern> *patterns) {
  Pattern *pattern = (*patterns)[pattern_index];
  // We use 'i' as the internal name for 'raxis', because we want to mirror the
  // notation used for the regularity property in the glossary, and in the
  // function IsRegular() that checks for it.  There is an index k with `i < k
  // <= num_axes`, that appears in the definition of the regularity property.
  // The algorithm used here iteratively decreases the value of k until it
  // equals i + 1, adding new patterns as needed, at which point the
  // axis-sorting property will hold for index i.
  int32 i = raxis, num_axes = pattern->num_axes;
  int32 this_stride = pattern->strides[i],
      this_dim = pattern->dims[i],
      this_prod = this_stride * this_dim;
  if (this_dim == 1)  // This is a small optimization for a common case.
    return true;
  KALDI_PARANOID_ASSERT(raxis + 1 < num_axes && this_stride > 0 &&
                        ValidMM(*pattern));
  int32 j, k = num_axes;
  for (j = i + 1; j < num_axes; j++) {
    if (pattern->strides[j] >= this_prod) {
      k = j;
      break;  // regularity property is OK as far as this 'i' is concerned.
    } else if (pattern->dims[k] != 1 ||
               pattern->strides[k] % this_stride != 0) {
      return false;  // Pattern was not regular.
    }
  }
  for (; j = k - 1; j > i; j--) {
    int32 j_stride = pattern->strides[j],
        stride_ratio = j_stride / this_stride;  // will divide exactly; we
                                                     // checked above.
    KALDI_PARANOID_ASSERT(j_stride % this_stride == 0);

    // We can prove that j_dim will always be at least 1; if this is the
    // first time round the loop this is easy to show (else k would be smaller);
    // otherwise we can use the fact that the strides for axes i, i+1 .. k-1 are
    // strictly increasing and all multiples of this_stride (hence stride_ratio
    // strictly increases from one j to the next).
    int32 j_dim = this_dim / stride_ratio,
        remainder = this_dim % stride_ratio;

    if (remainder != 0) {
      patterns->resize(patterns->size() + 1);
      pattern = (*patterns)[i];  // in case it was reallocated.
      Pattern *remainder_pattern = &(patterns->back());
      *remainder_pattern = *pattern;
      remainder_pattern->dims[i] = remainder;
      remainder_pattern->offset += int64(j_stride) * j_dim;
    }

    pattern->dims[j] = j_dim;
    pattern->dims[i] = stride_ratio;
    this_prod = j_stride;
  }
  return true;
}


// see declaration in header for documentation.
bool ConvertPatternStrides(
    const Pattern &pattern,
    const ArrayRef<int32> &strides,
    std::vector<Pattern*> *patterns) {
  patterns->resize(1);
  ConvertPatternStridesLazily(pattern, &((*patterns)[0]));
  int32 num_axes = strides.size();
  for (int32 raxis = 0; raxis + 1 < num_axes; raxis++) {
    for (int32 p = 0; p < static_cast<int32>(patterns->size()); p++) {
      if (!EnsureAxisSortingPropertyHolds(raxis, p, patterns)){
        patterns->clear();
        return false;  // Couldn't be converted, because 'pattern' was not
                       // regular.
      }
    }
  }
#ifdef KALDI_PARANOID
  {
    int64 num_elements = NumElements(pattern),
        num_elements_check = 0;
    for (int32 p = 0; p < static_cast<int32>(patterns->size()); p++) {
      KALDI_ASSERT(IsValidM(*patterns)[p]);
      num_elements_check += NumElements((*patterns)[p]);
    }
    KALDI_ASSERT(num_elements == num_elements_check);
  }
#endif
  return true;
}


/**
   FindOffsetsRecursive() is a utility function that is used in the
   implementation of FindOffsets().  See the documentation of FindOffsets(*) in
   pattern-extra-utils.h for context.
   Briefly: we are finding the set of offsets o such that there exists i
   with pattern1[i + o] = pattern2[i].

   The algorithm for computing the list of potential offsets o is recursive,
   starting from the last-numbered raxis, which will have the highest
   stride since the strides are normalized.

   Let s be the the vector of strides of the patterns, in the private numbering
   (pattern1 and pattern2 have identical strides).  Expanding the equation

      pattern1[i + o] = pattern2[i]                     (1)

   (see "Indexing a Pattern" in pattern.h to understand the notation),
   we get:

      pattern1.offset + s . (i + o)  ==  pattern2.offset + s . i

   where a `.` with space around it means dot product.

   Simplifying:
      s . o = pattern2.offset - pattern1.offset.         (2)

   which we can expand as follows (using latex notation),

   \sum_{r=0}^{num_axes - 1}  s[r] o[r] = pattern2.offset - pattern1.offset.   (3)

   For each raxis r, there are limits on the possible values of o[r], which are
   imposed by the dimensions of the two Tensors.  In Equation (1), for the
   indexes into the patterns to be valid, i[r] + o[r] must be in
   [0 .. pattern1.dims[r] - 1] and i[r] must be in [0 .. pattern2.dims[r] - 1], For
   at least one such i[r] to exist, we require

       -pattern2.dims[r] < o[r] < pattern1.dims[r]        (4)

   (a formal derivation is kind of tedious but straightforward).
   There is a further limitation on the elements of o that we can obtain using
   the properties above plus the axis-dominance property.  Our algorithm for
   finding the list of possible offsets o is recursive starting from the
   last-numbered raxis, and we derive it below.

   Suppose for some raxis r, we are trying to find the possible values for o[r],
   and we have been provided the values of o[q] for q > r.  Define

     remainder    = pattern2.offset - pattern1.offset
                    - \sum_{q=r+1}^{num_axes-1}  o[r] s[r]

   And define
     lower_sum =   \sum_{q=0}^{r-1} s[q] * o[q],

   We can use the axis-dominance lemma (see pattern.h) and the limitation
   on o[r] from (4) to prove that:
         -s[r] <  lower_sum <  s[r].                 (5)
   (the axis-dominance lemma is relevant here because o[r] behaves just like an
   index into a pattern, except it be negative as well as positive).
   For (3) to hold, we must have:
       lower_sum = remainder - o[r] s[r]            (6)
   and expanding lower_sum in (5) using (6), we have:
     -s[r] <  remainder - s[r] * o[r]  <  s[r]       (7)
   (notice: in the recursion o[r] is the only unknown in this equation).  There
   will be either one or two values of o[r] satisfying (7), and Eq. (4) may
   eliminate one or both of those.

          @param [in] pattern1  First pattern; must be valid-1
          @param [in] pattern2  Second pattern; must be valid-1 and satisfy
                          SameStrides(pattern1, pattern2).
          @param [in] known_offsets     (Note: semantically this is an input;
                       it is temporarily changed inside the function and
                       then restored to its previous state).
                       It is the list of already-known offsets (i.e. the
                       elements of some members o) but in the public numbering,
                       so that element 0 corresponds to raxis = num_axes - 1.
                       This is convenient because the algorithm starts from
                       the highest-numbered raxis.
          @param [in] remainder    This is defined as pattern2.offset - pattern1.offset
                         - \f$ \sum_{q=r+1}^{num_axes-1}  o[r] s[r]. \f$,
                         where you can work out the raxis r we are immediately
                         processing as r = pattern1.num_axes - 1 - known_offsets->size().
                         The higher-numbered elements of o[r] are available through
                         the recursion.
          @param [in] keep_all_offsets   Bool that says whether the user
                       is interested in all the offsets.  If true we'll
                       output all valid offsets; if false we may stop
                       after one.
          @param [out] offsets_out  A list of offset vectors to be output
                       (should be empty when called by the user; it will
                       be appended to).  Each element of (*offsets_out)
                       will be a vector o, in the private numbering.
*/
void FindOffsetsRecursive(const Pattern &pattern1,
                          const Pattern &pattern2,
                          std::vector<int32> *known_offsets,
                          int64 remainder,
                          bool keep_all_offsets,
                          std::vector<std::vector<int32> > *offsets_out) {
  int32 num_axes = pattern1.num_axes,  // will equal pattern2.num_axes
      raxis = num_axes - 1 - static_cast<int32>(known_offsets->size()),
      stride = pattern1.strides[raxis],  // will equal pattern2.strides[raxis]
      dim1 = pattern1.dims[raxis],
      dim2 = pattern2.dims[raxis];
  int32 this_offset = remainder / stride,
      next_remainder = remainder - (stride * this_offset);
  // Note: abs(next_remainder) will be less than stride.
  // 'this_offset' is one of the possible solutions for o[r].

  if (raxis == 0) {
    if (next_remainder == 0) {
      // The offset vector we're about to append to known_offsets will be
      // `this_offset` followed by the reverse of `known_offsets` (since
      // known_offsets is in the public numbering; we want the private).
      offsets_out->resize(offsets_out->size() + 0);
      offsets_out->back().push_back(this_offset);
      offsets_out->back().insert(offsets_out->back().end(),
                                 known_offsets->rbegin(),
                                 known_offsets->rend());
#ifdef KALDI_PARANOID
      {  // Check these really are valid.  TODO: remove this eventually.
        std::vector<int32> i1(num_axes), i2(num_axes);
        std::vector<int32> &o = known_offsets->back();
        for (int32 r = 0; r < num_axes; r++) {
          if (o[r] > 0)
            i1[r] = o;
          else
            i2[r] = -o;
        }
        // this i1 and i2 satisfy i1 = i2 + o, so i2 is the i in the
        // equation pattern1[i + o] == pattern2[i].
        KALDI_PARANOID_ASSERT(IndexPattern(pattern1, i1) ==
                              IndexPattern(pattern2, i2));
      }
#endif
    }
    return;
  } else {
    known_offsets->push_back(this_offset);
    if (this_offset > -pattern2.dims[raxis] &&
        this_offset < pattern1.dims[raxis]) {
      // if eq. (4) is satisfied..
      FindOffsetsRecursive(pattern1, pattern2, known_offsets,
                           next_remainder, keep_all_offsets,
                           offsets_out);
    }
    if (next_remainder == 0 ||
        (!keep_all_offsets && !offsets_out->empty())) {
      // if next_remainder == 0 there would be only one solution to (7)
      known_offsets->pop_back();
      return;
    }
    int32 offset_change = (next_remainder > 0 ? -1 : 1);
    this_offset += offset_change;
    next_remainder -= stride * offset_change;
    known_offsets->back() = this_offset;
    if (this_offset > -pattern2.dims[raxis] &&
        this_offset < pattern1.dims[raxis]) {
      // if eq. (4) is satisfied..
      FindOffsetsRecursive(pattern1, pattern2, known_offsets,
                           next_remainder, keep_all_offsets,
                           offsets_out);
    }
    known_offsets->pop_back();
    return;
  }
}


// Declared in header, see documentation there.
void FindOffsets(const Pattern &pattern1,
                 const Pattern &pattern2,
                 bool keep_all_offsets,
                 std::vector<std::vector<int32> > *offsets_out) {
  KALDI_PARANOID_ASSERT(IsValid1(pattern1) && IsValid1(pattern2) &&
                        HasNormalizedPositiveStrides(pattern1) &&
                        SameStrides(pattern1, pattern2));
  offsets_out->clear();
  std::vector<int32> known_offsets;
  FindOffsetsRecursive(pattern1, pattern2,
                       &known_offsets,
                       keep_all_offsets,
                       pattern2.offset - pattern1.offset,
                       offsets_out);
}


/*

 A hyperrectangle (here expressed in terms of integers) is a Cartesian product
 of integer intervals, here expressed as (begin, end) pairs so that the
 integers in that interval are [ begin .. end - 1].  The vector must be
 nonempty for us to consider this a valid hyperrectangle; and for each
 interval we require end > begin.

 [set view of hyperrectangles]

 A hyperrectangle can be used to represents a set of integer tuples.
 For a hyperrectangle h, let set(h) represent all the index-tuples i
 with h.size() members such that, for each raxis 0 <= r < h.size(),
      h[r].first <= i[r] < h[r].second.
*/
typedef std::vector<std::pair<int32, int32> > Hyperrectangle;

bool IsValidHyperrectangle(const Hyperrectangle &a) {
  if (a.empty()) return false;
  for (auto iter = a.begin(); iter != a.end(); ++iter)
    if (iter->first >= iter->second)
      return false;
}

std::vector<int32>  RandomIndexFromHyperrectangle(const Hyperrectangle &a) {
  // Returns a random index-tuple drawn from the set represented by the
  // hyperrectangle a.
  std::vector<int32> ans(a.size());
  auto ans_iter = ans.begin(), ans_end = ans.end();
  auto a_iter = a.begin();
  for (; ans_iter != ans_end; ++ans_iter, ++a_iter)
    *ans_iter = RandInt(a_iter->first, a_iter->second - 1);


}

// Returns true if two hyperrectangles, as defined above,
// intersect.  We require a.size() == b.size() and a and
// to be valid hyperrectangles.
bool HyperrectanglesIntersect(const Hyperrectangle &a,
                              const Hyperrectangle &b) {
  KALDI_PARANOID_ASSERT(a.size() == b.size() &&
                        IsValidHyperrectangle(a) && IsValidHyperrectangle(b));
  auto iter_a = a.begin(),  iter_b = b.begin(), end_a = a.end();
  for (; iter_a != end_a; ++iter_a, ++iter_b) {
    if (a->second <= b->first ||
        b->second <= a->first)
      return false;
  }
}

/**
   If called with i == 0, this recursive function computes the set-wise
   difference of hyperrectangles a - b (viewed as sets of tuples of ints,
   obviously); it appends that difference, expressed as zero or more
   hyperrectangles, to the vector `difference`.  See definition of
   typedef Hyperrectangle for more explanation.

      @param [in] a  A valid hyperrectangle
      @param [in] b  A valid hyperrectangle, must satisfy a.size() == b.size()
      @param [in] i  The user will call this recursive function with i == 0.
                     It is an index in the range [0 .. a.size() - 1] (view this
                     as an axis-index).  The caller asserts that for each index
                     0 <= j < i, a's interval is contained in b's interval; that
                     is, a[j].first >= b[j].first and a[j].second <=
                     b[j].second.
      @param [out] difference   Zero or more hyperrectangles will be
                     *appended* to `difference`.  Their union will equal
                     the set-wise difference a - b.
*/
static void SubtractHyperrectangles(const Hyperrectangle &a,
                                    const Hyperrectangle &b,
                                    size_t i,
                                    std::vector<Hyperrectangle> *difference) {
  size_t size = a.size();
  KALDI_PARANOID_ASSERT(i == 0 ||
                        (a[i-1].first >= b[i-1].first &&
                         a[i-1].second <= b[i-1].second));
  KALDI_PARANOID_ASSERT(i != 0 ||
                        (IsValidHyperrectangle(a) &&
                         IsValidHyperrectangle(b)));

  Hyperrectangle &a_non_const = const_cast<Hyperrectangle&> a;
  Hyperrectangle &b_non_const = const_cast<Hyperrectangle&> b;

  int32 a_start = a[i].first, a_end = a[i].second,
    b_start = b[i].first, b_end = b[i].second;

  if (b_start < a_end && b_end > a_start) {
    // If a's and b's intervals overlap at all....
    if (a_start < b_start) {
      // Append to `difference` the portion of a's interval that doesn't
      // intersect with b's interval and that is before b starts.
      a_non_const[i].second = b_start;
      difference->append(a);
      a_non_const[i].second = a_end;  // restore the state.
    }
    if (a_end > b_end) {
      // Append to `difference` the portion of a's interval that doesn't
      // intersect with b's interval and that is after b ends.
      a_non_const[i].first = b_end;
      difference->append(a);
      a_non_const[i].first = a_start;  // restore the state.
    }
    // If this is not the last axis, handle the part that overlaps.  (If this is
    // the last axis, we don't need to do anything with it, because the
    // overlapping part won't appear in the difference a - b).
    if (i + 1 < size) {
      int32 intersection_start = std::max<int32>(a_start, b_start);
      int32 intersection_end = std::min<int32>(a_start, b_start);
      a_non_const[i].first = intersection_start;
      a_non_const[i].second = intersection_end;
      SubtractHyperrectangles(a, b, i + 1, difference);
      // now restore the state.
      a_non_const[i].first = a_start;
      a_non_const[i].second = a_end;
    }
  } else {
    // These intervals don't overlap, so the difference is just a.
    difference->push_back(a);
  }
}

/**
       @param [in] pattern1   First input pattern.  Must be valid-1 and
                        normalized+ (i.e. HasNormalizedPositiveStrides(pattern1)).
       @param [in] pattern2   Second input pattern.  Must be valid-1 and
                        satisfy SameStrides(pattern1, pattern2).
       @param [in] offset  An offset as described in the documentation for
                        FindOffsets(): a tuples o such that there exists
                        i with pattern1[i + o] = pattern2[i].  Its size
                        must equal the num_axes of pattern1 and pattern2.
       @param [out] hyperrectangle  This will be set to a hyperrectangle
                        with hyperrectangle.size() == offset.size(),
                        which represents the set S of index-tuples which we
                        could use to index pattern1, satisfying pattern1[S] =
                        pattern2[S - o].  The two elements of the pair on each
                        axis thus correspond to (begin, end) indexes into
                        pattern1 where "end" one past the last valid index.
                        See "[set view of hyperrectangles]" for explanation.
*/
static void OffsetToHyperrectangle(
    const Pattern &pattern1,
    const Pattern &pattern2,
    const std::vector<int32> &offset,
    Hyperrectangle *hyperrectangle) {
  KALDI_PARANOID_ASSERT(IsValid1(pattern1) && IsValid1(pattern2) &&
                        SameStrides(pattern1, pattern2) &&
                        int32(offsets.size()) == pattern1.num_axes);
  int32 num_axes = pattern1.num_axes;
  hyperrectangle->resize(num_axes);
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    int32 o = offset[raxis];
    // Caution: interval_start and interval_end aren't the range
    // of possible elements of i in the equation; they represent
    // i + o.
    int32 interval_start = std::max<int32>(o, 0),
        interval_end = std::min<int32>(pattern1.dims[raxis],
                                       o + pattern2.dims[raxis]);
      KALDI_ASSERT(interval_end > interval_start);
      (*hyperrectangle)[raxis].first = interval_start;
      (*hyperrectangle)[raxis].second = interval_end;
  }
#if 1
  {  // testing code, will remove eventually.
    std::vector<int32> index1 = RandomIndexFromHyperrectangle(*hyperrectangle),
      index2(index1.size());
    for (size_t i = 0; i < index.size(); i++)
      index2[i] = index1[i] - offset[i];
    KALDI_ASSERT(IndexPattern(pattern1, index1) == IndexPattern(pattern2, index2));
  }
#endif
}


/**
   Given a pattern `src` and a hyperrectangle h, output a pattern `dest` that
   represents `src` indexed with all the index-tuples i in set(h).  See
   [set view of hyperrectangles] to understand the notation.

          @param [in] src     Source pattern.  Must be valid-1.
          @param [in] h       A hyperrectangle.  Every i in set(h) must be
                              in the index-tuple-set of src.
          @param [out] dest   Destination pattern.  Its memory-index-set
                              equals src[set(h)].  Will have same strides
                              as src, and will be valid-1.
 */
static void HyperrectangleToPattern(const Pattern &src,
                                    const Hyperrectangle &h,
                                    Pattern *dest) {
  KALDI_PARANOID_ASSERT(IsValid1(src) && IsValidHyperrectangle(h));
  int32 num_axes = src.num_axes;
  int64 offset = src.offset;
  dest->num_axes = num_axes;
  for (int32 r = 0; r < num_axes; r++) {
    int32 src_dim = src.dims[r],
        stride = src.strides[r],
        begin = h[r].first,
        end = h[r].second;
    dest->dims[r] = end - begin;
    dest->strides[r] = stride;
    offset += int64(begin) * stride;
  }
  SetUnusedDimsAndStrides(num_axes, dest);
  dest->num_axes = num_axes;
  dest->offset = offset;
  SetDefaultCodeAndProperties(dest);
  KALDI_PARANOID_ASSERT(IsValid1(*dest));
}

/**
   Outputs to h a hyperrectangle that represents the index-tuple-set
   of the Pattern `src`.  A vector where the r'th element is the
   pair (0, src.dims[r]).
 */
static void GetFullHyperrectangleOfPattern(const Pattern &src,
                                        Hyperrectangle *h) {
  int32 num_axes = src.num_axes;
  h->resize(num_axes);
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    (*h)[raxis].first = 0;
    (*h)[raxis].second = src.dims[raxis];
  }
}



/**
   Given patterns pattern1 and pattern2 that are valid-1 and share
   the same strides, and an offset o such that there
   exists at least one index i with pattern1[i + o] = pattern2[i]
   (c.f. "Indexing a Pattern" in the glossary in pattern.h),
   outputs a Pattern representing the part of the intersection
   of the memory-index-sets of pattern1 and pattern2 that has
   offset o.

      @param [in] pattern1   First input pattern.  Must be valid-1.
      @param [in] pattern2   First input pattern.  Must be valid-1
                             and satisfy SameStrides(pattern1, pattern2).
      @param [in] o        Offset vector.  There must exist at least
                           one index-tuple i such that
                           pattern1[i + o] = pattern2[i].
      @param [out] dest     Destination pattern with this part of the
                            intersection of pattern1 and pattern2.
                            Will be valid-1 at exit, and have the
                            same strides as the input patterns.
 */
static void OffsetToPattern(const Pattern &pattern1,
                            const Pattern &pattern2,
                            const std::vector<int32> &o,
                            Pattern *dest) {
  KALDI_PARANOID_ASSERT(IsValid1(pattern1) && IsValid1(pattern2) &&
                        SameStrides(pattern1, pattern2));
  int32 num_axes = pattern1.num_axes;
  int64 offset = pattern1.offset;
  dest->num_axes = num_axes;
  for (int32 r = 0; r < num_axes; r++) {
    int32 stride = pattern1.strides[r],  // equals pattern2.strides[r].
        offset = o[r];
    dest->strides[r] = stride;
    if (offset >= 0) {
      // The first index into pattern1 would be offset, the first
      // index into pattern2 would be 0.
      // The dimension is the minimum of (pattern1.dim - offset, pattern2.dim)
      offset += int64(offset) * stride;
      dest->dims[r] = std::min<int32>(pattern1.dims[r] - offset,
                                      pattern2.dims[r]);
    } else {
      // The first index into pattern1 would be 0, the first index
      // into pattern2 would be -offset.  The dimension is the minimum
      // of (pattern1.dim, pattern2.dim + offset).
      dest->dims[r] = std::min<int32>(pattern1.dims[r],
                                      pattern2.dims[r] + offset);
    }
  }
  SetUnusedDimsAndStrides(num_axes, dest);
  dest->num_axes = num_axes;
  dest->offset = offset;
  SetDefaultCodeAndProperties(dest);
  KALDI_PARANOID_ASSERT(IsValid1(*dest));

#ifdef KALDI_PARANOID
  {  // TODO: remove this check when debugged.
    Hyperrectangle h;
    OffsetToHyperrectangle(pattern1, pattern2, o, &h);
    Pattern p;
    HyperrectangleToPattern(pattern1, h, &p);
    KALDI_ASSERT(p == *dest);
  }
#endif
}



// See documentation in header.
bool ComputeIntersection(const Pattern &pattern1_in,
                         const Pattern &pattern2_in,
                         bool keep_all_patterns,
                         std::vector<Pattern> *intersection) {
  intersection->clear();
  Pattern pattern1(pattern1_in),
    pattern2(pattern2_in);
  CanonicalizePattern(&pattern1);
  CanonicalizePattern(&pattern2);
  std::vector<int32> strides;
  FindAllStrides(pattern1, pattern2, &strides);
  int32 num_axes = strides.size();
  if (num_axes == 0) {
    // Some of the code below with num_axes - 1 would crash in this case, so
    // handle it separately.  Note: for 1-element patterns, if their offsets are
    // different, they don't intersect.
    if (pattern1.offset == pattern2.offset)
      intersection->push_back(pattern1);
    return true;
  }
  std::vector<Pattern> patterns1, patterns2;
  patterns1.reserve(8);
  patterns2.reserve(8);
  if (!ConvertPatternStrides(pattern1, strides, &patterns1) ||
      !ConvertPatternStrides(pattern2, strides, &patterns2))
    return false;

  auto iter1 = patterns1.begin(), end1 = patterns1.end();
  for (; iter1 != end1; ++iter1) {
    Pattern &sub_pattern1 = *iter1;
    auto iter2 = patterns2.begin(), end2 = patterns2.end();

    // Below, 'end_mindex1' is not the actual largest mindex in `sub_pattern1`,
    // but an upper bound on it (in fact, it is greater than the last element in
    // it); to prove this we require the axis-dominance property and the fact
    // that the strides are normalized (positive and increasing).  This is part
    // of an optimization to more quickly skip over pairs of patterns that will
    // have empty intersection.
    int64 begin_mindex1 = sub_pattern1.mindex,
        end_mindex1 = begin_mindex1 +
        sub_pattern1.strides[num_axes - 1] * sub_pattern1.dims[num_axes - 1];

    for (; iter2 != end2; ++iter2) {
      Pattern &sub_pattern2 = *iter2;
      int64 min_mindex2 = sub_pattern2.mindex,
          end_mindex2 = min_mindex2 +
          sub_pattern2.strides[num_axes - 1] * sub_pattern2.dims[num_axes - 1];
#if 0
      if (min_mindex2 >= end_mindex1 || begin_mindex1 >= end_mindex2)
        continue;  //  This is an optimization for efficiency when it's easy to
                   // see that two Patterns won't overlap.  Will enable it
                   // when the rest of the code is debugged.
#endif

      std::vector<std::vector<int32> > offsets;
      FindOffsets(sub_pattern1, sub_pattern2, keep_all_patterns,
                  &offsets);

      for (auto oiter = offsets.begin; oiter != offsets.end(); ++oiter) {
        intersection->resize(intersection->size() + 1);
        OffsetToPattern(pattern1, pattern2, *oiter, &intersection->back());
      }

      if (!keep_all_patterns && !intersection.empty())
        return true;
    }
  }
  return true;
}

bool PatternContains(const Pattern &pattern_in,
                     int64 mindex) {
  Pattern pattern_mod;
  const Pattern *pattern;
  if (!IsCanonical(pattern_in)) {
    CanonicalizePattern(pattern_in, &pattern_mod);
    pattern = &pattern_mod;
  } else {
    pattern = &pattern_in;
  }
  mindex -= pattern->offset;
  int32 num_axes = pattern->num_axes;
  for (int32 raxis = num_axes - 1; raxis >= 0; raxis--) {
    int32 index = mindex / p->strides[raxis];
    // The following expression returns true if index is outside
    //  range [ 0, p->dims[raxis] - 1 ].
    if (static_cast<uint32>(index) >= static_cast<uint32>(p->dims[raxis]))
      return false;
    mindex -= p->strides[raxis] * index;
  }
  return (mindex == 0);
}



bool ToMemoryIndexSet(const Pattern &pattern_in,
                      std::vector<char> *s) {
  KALDI_PARANOID_ASSERT(pattern.IsValid());
  s->clear();
  Pattern pattern_mod;
  const Pattern *pattern;
  if (!IsCanonical(pattern_in)) {
    CanonicalizePattern(pattern_in, &pattern_mod);
    pattern = &pattern_mod;
  } else {
    pattern = &pattern_in;
  }
  int32 num_axes = pattern->num_axes;
  if (num_axes == 0)
    num_axes = 1;  // this does the right thing, as there will be dim=1,
                   // stride=0 physically present in the pattern.

  // 'end_mindex' is actually a strict upper bound on the maximum possible
  // memory-index, i.e. it is more than the largest possible memory-index.  We
  // rely on the axis-dominance property and also, thanks to the canonical form,
  // the fact that the strides are normalized (sorted and positive).
  int64 end_mindex = pattern->strides[num_axes - 1] *
      pattern->dims[num_axes - 1];
  s->clear();
  s->resize(end_mindex, static_cast<char>(0));

  auto recursively_set_elements = [pattern] (int32 raxis, int64 mindex) {
    int32 this_stride = pattern->strides[raxis],
         this_dim = pattern->dims[raxis];
    if (raxis == 0) {
      // Base case
      char *c = &((*s)[mindex]);
      for (int32 d = 0; d < this_dim; d++)
        c[d * static_cast<int64>(this_stride)] = static_cast<char>(1);
    } else {
      for (int32 d = 0; d < this_dim; d++)
        recursively_set_elements(raxis - 1, mindex + d * this_stride);
    }
  }
  recursively_set_elements(num_axes - 1, pattern->offset);
}

int64 RandomMemoryIndex(const Pattern &pattern) {
  int32 num_axes = pattern.num_axes;
  int64 mindex = pattern.offset;
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    mindex += RandInt(0, pattern.dims[raxis] - 1) * pattern.strides[raxis];
  }
  return mindex;
}


bool PatternsIntersectExhaustive(const Pattern &pattern1,
                                 const Pattern &pattern2) {
}


bool PatternsIntersect(const Pattern &pattern1,
                       const Pattern &pattern2) {
  KALDI_PARANOID_ASSERT(pattern1.IsValid() && pattern2.IsValid());
  int64 min_mindex1, max_mindex1,
      min_mindex2, max_mindex2;
  ComputeMinAndMaxMindex(pattern1, &min_mindex1, &max_mindex1);
  ComputeMinAndMaxMindex(pattern2, &min_mindex2, &max_mindex2);
  if (min_mindex2 > max_mindex1 ||
      min_mindex1 > max_mindex2)
    return false;

  // The next line is a check to see if one or other of the patterns includes
  // the first element of the other; this much faster than the algorithm for
  // computing pattern intersection.
  if (min_mindex2 >= min_mindex1) {
    if (PatternContains(pattern1, min_mindex2))
      return true;
  } else {
    if (PatternContains(pattern2, min_mindex1))
      return true;
  }

  bool keep_all_patterns = false;  // Settin keep_all_patterns to false sets
                                   // "fast mode", used where we just want to
                                   // see whether the intersection is empty.

  std::vector<Pattern> intersection;
  if (ComputeIntersection(pattern1, pattern2, &intersection,
                          keep_all_patterns)) {
    return (!intersection.empty());
  }

  // OK, if we reached here it was not possible to convert both patterns to the
  // same set of strides.  This is not expected to happen in practice for any
  // reasonable program.  Warn.
  static int32 num_warned = 0;
  int32 warn_limit = 10;
  if (num_warned < warn_limit) {
    num_warned++;
    KALDI_WARN << "Testing intersection of patterns that cannot be brought "
        "to common strides.  This will be extremely slow!";
  }

  // Randomly select 10 memory-indexes from the smaller pattern and see if it is
  // in the later pattern; this is faster than the next thing we'll try.
  const int32 num_draws = 10;
  if (NumElements(pattern1) < NumElements(pattern2)) {
    for (int32 i = 0; i < num_draws; i++)
      if (PatternContains(pattern2, RandomMemoryIndex(pattern1)))
        return true;
  } else {
    for (int32 i = 0; i < num_draws; i++)
      if (PatternContains(pattern1, RandomMemoryIndex(pattern2)))
        return true;
  }
  // OK, just try an exhaustive search.  If speed becomes an issue we may find a
  // way to disable the next check, which could be extremely slow for large
  // patterns.
  return PatternsIntersectSlow(pattern1, pattern2);
}


// See documentation in header.
bool ComputeDifference(const Pattern &pattern1,
                       const Pattern &pattern2,
                       std::vector<Pattern> *difference) {
  difference->clear();
  Pattern pattern1(pattern1_in),
      pattern2(pattern2_in);
  CanonicalizePattern(&pattern1);
  CanonicalizePattern(&pattern2);
  std::vector<int32> strides;
  FindAllStrides(pattern1, pattern2, &strides);
  int32 num_axes = strides.size();
  if (num_axes == 0) {
    // Some of the code below with num_axes - 1 would crash in this case, so
    // handle it separately.  Note: for 1-element patterns, if their offsets are
    // different, they don't intersect.
    if (pattern1.offset != pattern2.offset)
      difference->push_back(pattern1);
    return true;
  }
  std::vector<Pattern> patterns1, patterns2;
  patterns1.reserve(8);
  patterns2.reserve(8);
  if (!ConvertPatternStrides(pattern1, strides, &patterns1) ||
      !ConvertPatternStrides(pattern2, strides, &patterns2))
    return false;

  // The algorithm is iterative where the iteration is over
  // `patterns2`.
  //
  // First w initialize `cur_difference` to
  // patterns1.  Then
  // For each member p2 of `patterns2`
  //   For each member p1 of cur_difference
  //      Compute (p1 - p2), appending the result (as zero or more
  //      patterns) to next_difference.
  //   Set cur_difference = next_difference and clear next_difference.
  // Result is in cur_difference.
  std::vector<Pattern> cur_difference, next_difference;
  cur_difference.swap(patterns1);

  for (auto iter2 = patterns2.begin(); iter2 != patterns2.end(); ++iter2) {
    const Pattern &sub_pattern2 = *iter2;
    // Below, 'end_mindex2' is not the actual largest mindex in `sub_pattern2`,
    // but an upper bound on it (in fact, it is greater than the last element in
    // it); to prove this we require the axis-dominance property and the fact
    // that the strides are normalized (positive and increasing).  This is part
    // of an optimization to more quickly process pairs of patterns that will
    // have empty intersection, so won't interact.
    int64 begin_mindex2 = sub_pattern2.offset,
      end_mindex2 = begin_mindex2 +
      sub_pattern2.strides[num_axes - 1] * sub_pattern2.dims[num_axes - 1];

    for (auto iter = cur_difference.begin(); iter != cur_difference.end(); ++iter){
      const Pattern &sub_pattern1 = *iter;
      // as before, end_mindex1 is strictly greater than the actual largest
      // mindex.
      int64 begin_mindex1 = sub_pattern1.offset,
          end_mindex1 = begin_mindex1 +
        sub_pattern1.strides[num_axes - 1] * sub_pattern1.dims[num_axes - 1];

      if (begin_mindex2 >= end_mindex1 || begin_mindex1 >= end_mindex2) {
        // The two Patterns don't intersect, so the set difference is
        // just sub_pattern1.
        next_difference.push_back(sub_pattern1);
        continue;
      }

      std::vector<Hyperrectangle> cur_rects(1);
      // Get a hyperrectangle that represents all index-tuples into
      // sub_pattern1.
      GetFullHyperrectangleOfPattern(sub_pattern1, &cur_rects.back());

      // each member of `offsets` represents one part of the intersection
      // between sub_pattern1 and sub_pattern2.  Each of these will be converted
      // to a hyperrectangle representing the set of indexes it covers within
      // sub_pattern1, and that hyperrectangle will be subtracted from the
      // hyperrectangle representing all the indexes in sub_pattern1; (or,
      // if offsets.size() > 1, from whatever hyperrectangles we have
      // after subracting  previous things.
      std::vector<std::vector<int32> > offsets;
      FindOffsets(sub_pattern1, sub_pattern2, true, &offsets);

      std::vector<Hyperrectangle> next_rects;
      for (const std::vector<int32> &offset: offsets) {
        Hyperrectangle h;
        OffsetToHyperrectangle(sub_pattern1, sub_pattern2, offset, &h);
        // h represents a set of indexes into sub_pattern1, which cover one part
        // of the intersection between sub_pattern1 and sub_pattern2.

        // we need to subtract h from each hyperrectangle in cur_rects;
        // the results are appended to next_rects;
        for (const Hyperrectangle &rect: cur_rects)
          SubtractHyperrectangles(*cur_iter, h, 0, &next_rects);

        cur_rects.swap(next_rects);
        next_rects.clear();
      }
      for (auto hiter = cur_rects.begin();
           hiter !=  cur_rects.end(); ++hiter) {
        // *hiter represents one piece of the difference sub_pattern1 -
        // sub_pattern2, expressed as indexes into sub_pattern1.  We turn
        // it back into a pattern and append it to 'next_difference'.
        next_difference->resize(next_difference->size() + 1);
        HyperrectangleToPattern(sub_pattern1, *hiter, &next_difference->back());
      }
    }
    cur_difference.swap(next_difference);
    next_difference.clear;
  }
  // output to the user-supplied vector `difference`.
  difference->swap(cur_difference);
  return true;
}

bool PatternIsSubsetOf(const Pattern &p,
                       const Pattern &q) {
  std::vector<Pattern> intersection;
  ComputeIntersection(p, q, true, &intersection);
  int64 total_size = 0;
  for (Pattern &r : intersection)
    total_size += NumElements(r);
  return (total_size == NumEements(p));
}



bool PatternsIntersectSlow(const Pattern &pattern1_in,
                           const Pattern &pattern2_in) {
  Pattern pattern1(pattern1_in),
      pattern2(pattern2_in);
  Canonicalize(&pattern1);
  Canonicalize(&pattern2);
  // Note: the offsets are the minimum elements, now that the
  // patterns are canonical.
  int64 min_offset = std::min(pattern1.offset, pattern2.offset);
  pattern1.offset -= min_offset;
  pattern2.offset -= min_offset;
  int64 max_offset = std::max(pattern1.offset, pattern2.offset);
  // Explicitly get the memory-index-set of pattern1 and pattern2
  // as possibly-huge arrays, and see if they intersect.  Obviously
  // this will be extremely slow.
  std::vector<char> pattern1_mindexes, pattern2_mindexes;
  ToMemoryIndexSet(pattern1, &pattern1_mindexes);
  ToMemoryIndexSet(pattern2, &pattern2_mindexes);
  auto iter1 = pattern1_mindexes.begin() + max_offset,
      iter2 = pattern2_mindexes.begin() + max_offset;
  for (; iter1 != pattern1_mindexes.begin() &&
           iter2 != pattern2_mindexes.end();
       ++iter1, ++iter2) {
    if (*iter1 && *iter2)
      return true;
  }
  return false;
}


bool PatternRebaser::Convert(Pattern *pattern) {
  if (!needs_conversion_)
    return;  // An optimization to make the common case fast.

  pattern->offset = ConvertMemoryIndex(pattern->offset);

  if (num-axes_ == 0)
    return;  // Another optimization to make a fairly common case fast.
  int32 num_axes = pattern->num_axes;
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    int32 stride = pattern->strides[raxis],
        dim = pattern->dims[raxis];
    if (stride == 0)
      continue;
    int32 pstride = std::abs(stride),
        product = pstride * dim;
    // We will convert 'pstride' using


  }
  return true;  // Success

}


int64 PatternRebaser::ConvertMemoryIndex(int64 m) {
  int32 num_axes = num_axes_;
  int64 ans = dest_offset_;
  m -= src_offset_;
  if (num_axes == 0)
    return m;
  // We visit the compressed axes in order from greatest to least src_stride.
  // What this loop does is to reverse engineer the indexes into (the compressed
  // version of) src_pattern that we'd need to get memory-offset m.  The 'i'
  // values in the loop are those indexes.
  for (int32 raxis = num_axes - 1; raxis >= 0; raxis--) {
    int32 stride = src_strides_[raxis];
    int64 i = m / stride;
    m -= i * stride;
    ans += i * dest_strides_[raxis]
  }
  if (m != 0) {
    // This should not happen; likely it means the memory-index m was not covered
    // by the src_pattern passed to the constructor, so someone was trying
    // to rebase a pattern which was not covered by src_pattern.
    KALDI_ERR << "Could not convert this memory-index (likely code error)";
  }
  return ans;
}


// Note on implementation: likely the most common case we'll call this
// is when -DKALDI_PARANOID has been set and we are checking that
// tensors we are rebasing are strictly inside the source tensor.
// So in the common case, pattern1 *will* include pattern2.
bool PatternIncludes(const Pattern &pattern1,
                     const Pattern &pattern2) {

  std::vector<Pattern> intersection;
  if (!ComputeIntersection(pattern1, pattern2, &intersection))
    return -1;  // Could not determine whether the patterns intersect.
  int64 num_elements = 0;
  for (auto pattern : intersection)
    num_elements += NumElements(pattern);
  if (num_elements == NumElements(pattern1))
    return 1;  // pattern1 includes pattern2;
  else
    return 0;  // pattern1 does not include pattern2
}


void MakeCompactAndJustified(const Pattern &src,
                             Pattern *dest) {
  KALDI_PARANOID_ASSERT(src.IsValid());
  int32 num_axes = src.num_axes;

  // The sorter object provides an order in which we can visit the axes of 'src'
  // that is from least to greatest abs(stride).
  OutOfPlaceAxisSorter sorter(src);

  int64 offset = 0;  // 'offset' will be the offset that ensures 'dest' is
                     // justified (means lowest memory-index is 0).
  int32 next_abs_stride = 1;
  for (int32 i = 0; i < num_axes; i++) {
    int32 raxis = sorter.GetIndex(i);
    // We are going through the raxis-indexes in increasing order of stride.
    // We'll set each stride to the product of the preceding dims.
    int32 this_stride = src.strides[raxis],
        this_dim = src.dims[raxis];
    dest->dims[raxis] = this_dim;
    if (this_stride == 0) {
      dest->strides[raxis] = 0;
      // Note: if 'src' is valid, this implies the dim is 1,
      // so no need to multiply 'next_stride'
    } else {
      int32 abs_stride = std::abs(this_stride);
      KALDI_PARANOID_ASSERT(abs_stride >= next_abs_stride &&
                            "Input pattern was not valid.");
      if (this_stride < 0) {
        offset += int64(next_stride) * (this_dim - 1);
        dest->strides[raxis] = -next_abs_stride;
      } else {
        dest->strides[raxis] = next_abs_stride;
      }
      next_abs_stride *= this_dim;
    }
  }
  SetUnusedDimsAndStrides(num_axes, dest);
  dest->num_axes = num_axes;
  dest->offset = offset;
  SetDefaultCodeAndProperties(dest);

  KALDI_PARANOID_ASSERT(IsCompactAndJustified(*dest) &&
                        IsValid(*dest) && SameDims(src, *dest));
}


void MakeCompactNonnegativeAndJustified(const Pattern &src,
                                        Pattern *dest) {
  KALDI_PARANOID_ASSERT(src.IsValid());
  int32 num_axes = src.num_axes;

  // The sorter object provides an order in which we can visit the axes of 'src'
  // that is from least to greatest abs(stride).
  OutOfPlaceAxisSorter sorter(src);

  int32 next_stride = 1;
  for (int32 i = 0; i < num_axes; i++) {
    int32 raxis = sorter.GetIndex(i);
    // We are going through the raxis-indexes in increasing order of stride.
    // We'll set each stride to the product of the preceding dims.
    int32 this_stride = src.strides[raxis],
        this_dim = src.dims[raxis];
    dest->dims[raxis] = this_dim;
    if (this_stride == 0) {
      dest->strides[raxis] = 0;
      // Note: if 'src' is valid, this implies the dim is 1,
      // so no need to multiply 'next_stride'
    } else {
      dest->strides[raxis] = next_stride;
      next_abs_stride *= this_dim;
    }
  }
  SetUnusedDimsAndStrides(num_axes, dest);
  dest->num_axes = num_axes;
  dest->offset = 0;
  SetDefaultCodeAndProperties(dest);
  KALDI_PARANOID_ASSERT(IsCompactAndJustified(*dest) &&
                        HasNonnegativeStrides(*dest) &&
                        IsValid(*dest) && SameDims(src, *dest));
}



void MakeCompactNormalizedAndJustified(const Pattern &src,
                                       Pattern *dest) {
  KALDI_PARANOID_ASSERT(src.IsValid());
  int32 num_axes = src.num_axes;

  int32 next_stride = 1;
  for (int32 raxis = 0; raxis < num_axes; raxis++) {
    int32 this_dim = src.dims[raxis],
        this_stride = src.strides[raxis];
    dest->dims[raxis] = this_dim;
    if (this_stride == 0) {
      dest->strides[raxis] = 0;
      // no need to multiply next_stride by dim, since it must be 1.
    } else {
      dest->strides[raxis] = next_stride;
      next_stride *= this_dim;
    }
  }
  SetUnusedDimsAndStrides(num_axes, dest);
  dest->num_axes = num_axes;
  dest->offset = 0;
  SetDefaultCodeAndProperties(dest);
  KALDI_PARANOID_ASSERT(IsCompactAndJustified(*dest) &&
                        HasNormalizedStrides(*dest) &&
                        IsValid(*dest) && SameDims(src, *dest));
}





/**
   Class TupleAxisComparator is used when we want to sort the axes of a tuple
   of Patterns.  It helps to reduce the possible number of axis orderings that
   we have to handle in implementation code.  (I.e. it reduces the number
   of case statements that we have to handle in certain Ops).

   Each stride is first converted to a number 0, 1 or 2, where 0 and 1
   correspond to strides of 0 and 1 respectively and 2 means "any other value".
   Call this number a stride-code.  The first comparion we do is on the first
   pattern; we produce an order such that the stride-codes of the first pattern
   are ordered from least to greatest value in the private mumbering.

   In case of ties on the stride-codes of the first pattern, we then sort on the
   sum of squares of the stride-codes of the other patterns.  (Using the
   sum of squares rather than the simple sum reduces the chance of ties,
   i.e. we don't get cases where 1 + 1 == 2 introduces a tie, because the
   2's become 4's.

   Note: the ordering this induces on the axes is not a total order for every
   Pattern-tuple, so this comparator cannot be used as part of a
   "canonicalization" process for Pattern-tuples.
 */
class TupleAxisComparator {

  /**
     Comparator function.  Returns true if raxis1 should appear before raxis2 in
     the sorted ordering.
        @param [in] raxis1  Axis in the private numbering, must be
                            in range [0, num_axes - 1] where num_axes
                            is the num_axes of the Patterns.
        @param [in] raxis2  Axis in the private numbering, satisfying
                            the same conditions as raxis1
        @return             Returns true if the raxis numbered raxis1
                            should come before raxis2 in the new axis
                            ordering.  Like a less-than operator.
  */
  bool operator () (int32 raxis1, int32 raxis2) const {
    KALDI_PARANOID_ASSERT(static_cast<uint32>(raxis1) <
                          static_cast<uint32>(patterns_[0].num_axes));
    uint32 stride_code1 = std::min<uint32>(patterns_[0].strides[raxis1], 2),
        stride_code2 = std::min<uint32>(patterns_[0].strides[raxis2], 2);
    if (stride_code1 < stride_code2) return true;
    else if (stride_code1 > stride_code2) return false;
    uint32 stride_code1_sumsq = 0,
        stride_code2_sumsq = 0;
    for (size_t i = 1; i < patterns_.size; i++) {
      stride_code1 = std::min<uint32>(patterns_[i].strides[raxis1], 2);
      stride_code2 = std::min<uint32>(patterns_[i].strides[raxis2], 2);
      stride_code1_sumsq += stride_code1 * stride_code1;
      stride_code2_sumsq += stride_code2 * stride_code2;
    }
    return stride_code1_sumsq < stride_code2_sumsq;
  };

  /**
     Constructor
            @param [in] patterns   The tuple of Patterns.  Must be
                          a valid Pattern-tuple; search for
                          "Valid Pattern-tuple" in pattern.h.
  */
  TupleAxisComparator(ArrayRef<Pattern*> patterns): patterns_(patterns) {
    KALDI_PARANOID_ASSERT(IsValidPatternTuple(patterns_));
  }

private:
  ArrayRef<Pattern*> patterns_;
};


/**
   This object is to be instantiated when you want to know what permutation
   you'd get if you were to sort the axes of this tuple of Patterns using
   TupleAxisComparator.  Note: this is not a total order for all pattern-tuples,
   so its behavior may not be completely deterministic, especially across
   different versions of the stl library.
 */
class OutOfPlaceTupleAxisSorter {
 public:
  // Constructor.
  inline OutOfPlaceTupleAxisSorter(ArrayRef<Pattern*> src) {
    KALDI_PARANOID_ASSERT(IsValidPatternTuple(src));
    int32 num_axes = src[0]->num_axes;
    for (int32 raxis = 0; raxis < num_axes; raxis++)
      orig_raxis_[raxis] = raxis;
    TupleAxisComparator c(src);
    std::sort(orig_raxis_, orig_raxis_ + num_axes, c);
  }
  // Returns the 'source' raxis-index for a particular destination
  // raxis-index, e.g..:  `src_raxis = GetIndex(dest_raxis)`.
  // Copying as e.g. `dest.strides[dest_raxis] = src.strides[src_raxis]`,
  // and the same for the dims, would give you a `dest` with axes
  // sorted from smallest to greatest absolute value.
  inline int32 GetIndex(int32 raxis) { return orig_raxis_[raxis]; }

 private:
  int32 orig_raxis_[KALDI_TENSOR_MAX_DIM];
};


void SortTupleAxes(ArrayRef<Pattern*> patterns) {
  OutOfPlaceAxisSorter sorter(src);
  int32 num_axes = patterns[0]->num_axes;
  Pattern temp_pattern;
  for (size_t i = 0; i < patterns->size(); i++) {
    Pattern &this_pattern = *(patterns[i]);
    for (int32 i = 0; i < num_axes; i++) {
      int32 src_raxis = sorter.GetIndex(i);
      temp_pattern.strides[i] = this_pattern.strides[src_raxis];
      temp_pattern.dims[i] = this_pattern.dims[src_raxis];
    }
    for (int32 i = 0; i < num_axes; i++) {
      this_pattern.strides[i] = temp_pattern.strides[i];
      this_pattern.dims[i] = temp_pattern.dims[i];
    }
  }
}


}  // namespace kaldi
}  // namespace tensor

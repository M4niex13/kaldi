// nnet3/nnet-optimize-utils.cc

// Copyright      2015  Johns Hopkins University (author: Daniel Povey)

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

#include <map>
#include "nnet3/nnet-optimize-utils.h"
#include "nnet3/nnet-optimize.h"


namespace kaldi {
namespace nnet3 {


void IdentifySubmatrixArgs(NnetComputation::Command *c,
                           std::vector<int32*> *submatrix_args) {
  submatrix_args->clear();
  switch (c->command_type) {
    case kAllocMatrixZeroed:
    case kAllocMatrixUndefined:
    case kDeallocMatrix:
      submatrix_args->push_back(&c->arg1);
      break;
    case kAllocMatrixFromOther:
    case kAllocMatrixFromOtherZeroed:
      submatrix_args->push_back(&c->arg1);
      submatrix_args->push_back(&c->arg2);
      break;
    case kPropagate:
      submatrix_args->push_back(&c->arg3);
      submatrix_args->push_back(&c->arg4);
      break;
    case kStoreStats:
      submatrix_args->push_back(&c->arg2);
      break;
    case kBackprop:
    case kBackpropNoModelUpdate:
      submatrix_args->push_back(&c->arg3);
      submatrix_args->push_back(&c->arg4);
      submatrix_args->push_back(&c->arg5);
      submatrix_args->push_back(&c->arg6);
      break;
    case kMatrixCopy:
    case kMatrixAdd:
    case kAddRows:
    case kCopyRows:
    case kAddRowRanges:
      submatrix_args->push_back(&c->arg1);
      submatrix_args->push_back(&c->arg2);
      break;
    case kAddRowsMulti:
    case kCopyRowsMulti:
    case kAddToRowsMulti:
    case kCopyToRowsMulti:
      submatrix_args->push_back(&c->arg1);
      break;
    case kAcceptInput: case kProvideOutput:
      submatrix_args->push_back(&c->arg1);
      break;
    case kNoOperation:
    case kNoOperationMarker:
    case kNoOperationLabel:
    case kGotoLabel:
      break;
    default:
      KALDI_ERR << "Unknown command type.";
  }
}

void IdentifySubmatrixArgs(std::vector<NnetComputation::Command> *commands,
                           std::vector<int32*> *submatrix_args) {
  submatrix_args->clear();
  std::vector<NnetComputation::Command>::iterator iter = commands->begin(),
      end = commands->end();
  std::vector<int32*> this_submatrix_args;
  for (; iter != end; ++iter) {
    IdentifySubmatrixArgs(&(*iter), &this_submatrix_args);
    submatrix_args->insert(submatrix_args->end(),
                           this_submatrix_args.begin(),
                           this_submatrix_args.end());
  }
}



void IdentifyMatrixArgsInComputation(NnetComputation *computation,
                                     std::vector<int32*> *matrix_args) {
  int32 num_submatrices = computation->submatrices.size();
  matrix_args->reserve(computation->submatrices.size());
  for (int32 s = 1; s < num_submatrices; s++)
    matrix_args->push_back(&(computation->submatrices[s].matrix_index));
}


void IdentifyIndexesMultiArgs(std::vector<NnetComputation::Command> *commands,
                              std::vector<int32*> *indexes_multi_args) {
  indexes_multi_args->clear();
  std::vector<NnetComputation::Command>::iterator iter = commands->begin(),
      end = commands->end();
  for (; iter != end; ++iter) {
    NnetComputation::Command &command = *iter;
    if (command.command_type == kAddRowsMulti ||
        command.command_type == kAddToRowsMulti ||
        command.command_type == kCopyRowsMulti ||
        command.command_type == kCopyToRowsMulti)
      indexes_multi_args->push_back(&(command.arg2));
  }
}


void IdentifyIndexesRangesArgs(std::vector<NnetComputation::Command> *commands,
                               std::vector<int32*> *indexes_ranges_args) {
  indexes_ranges_args->clear();
  std::vector<NnetComputation::Command>::iterator iter = commands->begin(),
      end = commands->end();
  for (; iter != end; ++iter) {
    NnetComputation::Command &command = *iter;
    if (command.command_type == kAddRowRanges)
      indexes_ranges_args->push_back(&(command.arg3));
  }
}

void IdentifyIndexesArgs(std::vector<NnetComputation::Command> *commands,
                         std::vector<int32*> *indexes_args) {
  indexes_args->clear();
  std::vector<NnetComputation::Command>::iterator iter = commands->begin(),
      end = commands->end();
  for (; iter != end; ++iter) {
    NnetComputation::Command &command = *iter;
    if (command.command_type == kCopyRows ||
        command.command_type == kAddRows)
      indexes_args->push_back(&(command.arg3));
  }
}



// static
int32 ComputationRenumberer::CreateRenumbering(
    const std::vector<bool> &used,
    std::vector<int32> *renumbering) {
  renumbering->clear();
  renumbering->reserve(used.size());
  std::vector<bool>::const_iterator iter = used.begin(), end = used.end();
  int32 cur_index = 0;
  for (; iter != end; ++iter) {
    if (*iter) renumbering->push_back(cur_index++);
    else renumbering->push_back(-1);
  }
  return cur_index;
}

// static
void ComputationRenumberer::CreateRenumbering(
    int32 old_num_elements,
    const std::vector<int32> &to_remove,
    std::vector<int32> *renumbering) {
  KALDI_ASSERT(IsSortedAndUniq(to_remove) && old_num_elements > 0);
  renumbering->clear();
  renumbering->resize(old_num_elements, 0);
  int32 num_remove = to_remove.size();
  for (int32 r = 0; r < num_remove; r++) {
    int32 this_remove = to_remove[r];
    // the "> 0" would be ">= 0" in a more generic context, but zero is
    // not valid in this particular application.
    KALDI_ASSERT(this_remove > 0 && this_remove < old_num_elements);
    (*renumbering)[this_remove] = -1;
  }
  int32 cur_number = 0;
  for (int32 i = 0; i < old_num_elements; i++) {
    if ((*renumbering)[i] != -1)
      (*renumbering)[i] = cur_number++;
  }
  KALDI_ASSERT(cur_number == old_num_elements -
               static_cast<int32>(to_remove.size()));
}


void IdentifySubmatrixArgsInComputation(NnetComputation *computation,
                                        std::vector<int32*> *submatrix_args) {
  IdentifySubmatrixArgs(&(computation->commands), submatrix_args);

  size_t extra_size = 0;
  for (size_t i = 0; i < computation->indexes_multi.size(); i++)
    extra_size += computation->indexes_multi[i].size();
  submatrix_args->reserve(submatrix_args->size() + extra_size);

  for (size_t i = 0; i < computation->indexes_multi.size(); i++) {
    std::vector<std::pair<int32, int32> > &indexes_multi =
        computation->indexes_multi[i];
    std::vector<std::pair<int32, int32> >::iterator
        iter = indexes_multi.begin(), end = indexes_multi.end();
    for (; iter != end; ++iter)
      if (iter->first != -1)
        submatrix_args->push_back(&(iter->first));
  }
}


void ComputationRenumberer::ComputeSubmatrixIsUsed() {
  int32 num_submatrices = computation_->submatrices.size();
  submatrix_is_used_.clear();
  submatrix_is_used_.resize(num_submatrices, false);
  submatrix_is_used_[0] = true;
  // the zeroth element of the array is 'special', it refers to the
  // zero submatrix, and we don't want to renumber it.
  std::vector<int32*> submatrix_args;
  IdentifySubmatrixArgsInComputation(computation_, &submatrix_args);
  std::vector<int32*>::iterator iter = submatrix_args.begin(),
      end = submatrix_args.end();
  int32 cur_submatrix_index = -1;  // an optimization to avoid too many
                                   // indexings of the bool vector
                                   // submatrix_is_used_.
  for (; iter != end; ++iter) {
    int32 submatrix_index = **iter;
    if (submatrix_index > 0 && submatrix_index != cur_submatrix_index) {
      cur_submatrix_index = submatrix_index;
      KALDI_ASSERT(submatrix_index < num_submatrices);
      submatrix_is_used_[submatrix_index] = true;
    }
  }
}

void ComputationRenumberer::ComputeMatrixIsUsed() {
  matrix_is_used_.clear();
  matrix_is_used_.resize(computation_->matrices.size(), false);
  matrix_is_used_[0] = true;
  // We also need to take into account when matrices are used indirectly via
  // submatrices (which is actually the main way they are accessed).
  int32 num_submatrices = computation_->submatrices.size();
  for (int32 s = 1; s < num_submatrices; s++) {
    int32 matrix_index = computation_->submatrices[s].matrix_index;
    if (submatrix_is_used_[s])
      matrix_is_used_[matrix_index] = true;
  }
}



void ComputationRenumberer::SetUpMappings() {
  num_matrices_new_ = CreateRenumbering(matrix_is_used_, &old_to_new_matrix_);

  unordered_map<NnetComputation::SubMatrixInfo, int32,
                SubMatrixHasher> submat_map;
  int32 cur_index = 1, num_submatrices_orig =
      computation_->submatrices.size();
  // the old_to_new_submatrix_ map will remove duplicates.
  // -1's will appear wherever a particular submatrix was never used.
  submatrix_is_kept_ = submatrix_is_used_;
  old_to_new_submatrix_.resize(num_submatrices_orig, -1);
  old_to_new_submatrix_[0] = 0;
  for (int32 s = 1; s < num_submatrices_orig; s++) {
    if (submatrix_is_used_[s]) {
      const NnetComputation::SubMatrixInfo &info =
          computation_->submatrices[s];
      if (submat_map.count(info) > 0) {  // a duplicate...
        old_to_new_submatrix_[s] = submat_map[info];
        submatrix_is_kept_[s] = false;
      } else {
        old_to_new_submatrix_[s] = (submat_map[info] = cur_index++);
      }
    }
  }
  num_submatrices_new_ = cur_index;
}

void ComputationRenumberer::RenumberSubmatrices() {
  std::vector<int32*> submatrix_args;
  IdentifySubmatrixArgsInComputation(computation_, &submatrix_args);
  std::vector<int32*>::iterator iter = submatrix_args.begin(),
      end = submatrix_args.end();
  for (; iter != end; ++iter) {
    if (**iter > 0) {
      int32 new_submatrix_index = old_to_new_submatrix_[**iter];
      // old_to_new_submatrix_[s] for s > 0 is only <= 0 (actually, -1) for
      // submatrices that are never accessed, and these should never appear
      // in this list.
      KALDI_ASSERT(new_submatrix_index > 0);
      **iter = new_submatrix_index;
    }
  }
  std::vector<NnetComputation::SubMatrixInfo> new_submatrices;
  int32 num_submatrices_old = computation_->submatrices.size();
  new_submatrices.reserve(num_submatrices_old);
  for (int32 s = 0; s < num_submatrices_old; s++)
    if (submatrix_is_kept_[s])
      new_submatrices.push_back(computation_->submatrices[s]);
  computation_->submatrices.swap(new_submatrices);
  // We'll map the matrix indexes inside computation_->submatrices
  // when we call RenumberMatrices().
}

void ComputationRenumberer::RenumberMatrices() {
  std::vector<int32*> matrix_args;
  int32 num_submatrices = computation_->submatrices.size();
  for (int32 s = 1; s < num_submatrices; s++) {
    int32 *matrix_index = &(computation_->submatrices[s].matrix_index);
    // old_to_new_matrix_[s] for s > 0 is only <= 0 (actually, -1) for
    // submatrices that are never accessed, and these should never appear
    // in this list.  (presumably because we renumber the submatrices first).
    int32 new_matrix_index = old_to_new_matrix_[*matrix_index];
    KALDI_ASSERT(new_matrix_index > 0);
    *matrix_index = new_matrix_index;
  }

  std::vector<NnetComputation::MatrixInfo> new_matrices;
  int32 num_matrices_old = computation_->matrices.size();
  new_matrices.reserve(num_matrices_old);
  for (int32 m = 0; m < num_matrices_old; m++)
    if (matrix_is_used_[m])
      new_matrices.push_back(computation_->matrices[m]);
  computation_->matrices.swap(new_matrices);

  std::vector<NnetComputation::MatrixDebugInfo> new_debug_info;
  int32 debug_info_size = computation_->matrix_debug_info.size();
  KALDI_ASSERT(debug_info_size == 0 || debug_info_size == num_matrices_old);
  new_debug_info.reserve(debug_info_size);
  for (int32 m = 0; m < debug_info_size; m++) {
    if (matrix_is_used_[m]) {
      new_debug_info.push_back(NnetComputation::MatrixDebugInfo());
      new_debug_info.back().Swap(&(computation_->matrix_debug_info[m]));
    }
  }
  computation_->matrix_debug_info.swap(new_debug_info);
}


void ComputationRenumberer::Renumber() {
  RemoveUnusedIndexesMulti();
  ComputeSubmatrixIsUsed();
  ComputeMatrixIsUsed();
  SetUpMappings();
  RenumberSubmatrices();
  RenumberMatrices();
  RemoveIndexesMultiDuplicates();
  RenumberIndexes();
  RenumberIndexesRanges();
}

void ComputationRenumberer::RemoveUnusedIndexesMulti() {
  int32 num_indexes_multi = computation_->indexes_multi.size();
  if (num_indexes_multi == 0)
    return;  // Nothing to do.  An optimization.
  std::vector<bool> indexes_multi_used(num_indexes_multi, false);
  std::vector<int32*> indexes_multi_args;
  IdentifyIndexesMultiArgs(&(computation_->commands), &indexes_multi_args);
  std::vector<int32*>::iterator iter = indexes_multi_args.begin(),
      end = indexes_multi_args.end();
  for (; iter != end; ++iter) {
    int32 indexes_multi_index = **iter;
    KALDI_ASSERT(indexes_multi_index >= 0 &&
                 indexes_multi_index < num_indexes_multi);
    indexes_multi_used[indexes_multi_index] = 1;
  }
  // old->new mapping for the indexes_multi arrays.  will remain -1 for
  // ones that are unused.
  std::vector<int32> old_to_new(num_indexes_multi, -1);
  int32 new_num_indexes_multi = CreateRenumbering(indexes_multi_used,
                                                  &old_to_new);
  if (new_num_indexes_multi == num_indexes_multi)
    return;  // Nothing to do.  An optimization.
  std::vector<std::vector<std::pair<int32, int32> > >
      new_indexes_multi(new_num_indexes_multi);
  for (int32 i = 0; i < num_indexes_multi; i++) {
    if (old_to_new[i] != -1)
      new_indexes_multi[old_to_new[i]].swap(computation_->indexes_multi[i]);
  }
  computation_->indexes_multi.swap(new_indexes_multi);
  // renumber within the commands.
  for (iter = indexes_multi_args.begin(); iter != end; ++iter)
    **iter = old_to_new[**iter];
}


// removes duplicates within the indexes_multi_ array itself.
void ComputationRenumberer::RemoveIndexesMultiDuplicates() {
  int32 cur_index = 0,
      old_indexes_multi_size = computation_->indexes_multi.size();
  if (old_indexes_multi_size == 0)
    return;
  // create index mapping from old to new.  the use of map is generally not that
  // efficient, but the idea here is that we can do most of the comparisons just
  // based on the size of the vectors, and avoid even visiting most of their
  // contents.
  std::vector<int32> indexes_multi_old_to_new(old_indexes_multi_size);
  typedef std::vector<std::pair<int32,int32> > PairVectorType;
  typedef std::map<const PairVectorType*, int32,
                   PointerCompare<std::pair<int32,int32> > > MapType;
  MapType indexes_multi_map;
  for (int32 i = 0; i < computation_->indexes_multi.size(); i++) {
    std::pair<MapType::iterator, bool> p =
        indexes_multi_map.insert(std::pair<const PairVectorType*, int32>(
            &(computation_->indexes_multi[i]), cur_index));
    if (p.second) {  // was inserted-- was not there already.
      indexes_multi_old_to_new[i] = cur_index++;
    } else {
      int32 index_from_map = p.first->second;
      indexes_multi_old_to_new[i] = index_from_map;
    }
  }
  if (cur_index == old_indexes_multi_size)
    return;  // An optimization.  No duplicates were found.
  std::vector<PairVectorType> new_indexes_multi(cur_index);
  for (int32 i = 0; i < old_indexes_multi_size; i++) {
    int32 new_index = indexes_multi_old_to_new[i];
    computation_->indexes_multi[i].swap(new_indexes_multi[new_index]);
  }
  computation_->indexes_multi.swap(new_indexes_multi);

  std::vector<int32*> indexes_multi_args;
  IdentifyIndexesMultiArgs(&(computation_->commands), &indexes_multi_args);
  std::vector<int32*>::const_iterator iter = indexes_multi_args.begin(),
      end = indexes_multi_args.end();
  for (; iter != end; ++iter)
    **iter = indexes_multi_old_to_new[**iter];
}


void ComputationRenumberer::RenumberIndexes() {
  int32 old_num_indexes = computation_->indexes.size();
  if (old_num_indexes == 0)
    return;
  std::vector<int32*> indexes_args;
  IdentifyIndexesArgs(&(computation_->commands), &indexes_args);

  std::vector<bool> indexes_seen(old_num_indexes, false);
  std::vector<int32*>::const_iterator iter = indexes_args.begin(),
      end = indexes_args.end();
  for (; iter != end; ++iter)
    indexes_seen[**iter] = true;

  std::vector<int32> old_to_new_index(old_num_indexes);
  typedef std::map<const std::vector<int32>*, int32,
                   PointerCompare<int32> > MapType;
  MapType indexes_map;

  int32 cur_index = 0;
  for (int32 i = 0; i < old_num_indexes; i++) {
    if (!indexes_seen[i]) {
      old_to_new_index[i] = -1;
    } else {
      std::pair<MapType::iterator, bool> p =
          indexes_map.insert(std::pair<const std::vector<int32>*, int32>(
              &(computation_->indexes[i]), cur_index));
      if (p.second) {  // was inserted-- was not there already.
        old_to_new_index[i] = cur_index++;
      } else {
        int32 index_from_map = p.first->second;
        old_to_new_index[i] = index_from_map;
      }
    }
  }
  if (cur_index == old_num_indexes)
    return;  // An optimization.  No changes to the numbering are made.
  std::vector<std::vector<int32> > new_indexes(cur_index);
  for (int32 i = 0; i < old_num_indexes; i++) {
    int32 new_index = old_to_new_index[i];
    if (new_index != -1)
      computation_->indexes[i].swap(new_indexes[new_index]);
  }
  computation_->indexes.swap(new_indexes);

  // renumber the indexes inside the commmands.
  for (iter = indexes_args.begin(); iter != end; ++iter) {
    int32 old_index = **iter;
    KALDI_ASSERT(old_index >= 0 && old_index < old_num_indexes);
    int32 new_index = old_to_new_index[old_index];
    KALDI_ASSERT(new_index >= 0);
    **iter = new_index;
  }
}

void ComputationRenumberer::RenumberIndexesRanges() {
  int32 old_num_indexes_ranges = computation_->indexes_ranges.size();
  if (old_num_indexes_ranges == 0)
    return;
  std::vector<int32*> indexes_ranges_args;
  IdentifyIndexesRangesArgs(&(computation_->commands), &indexes_ranges_args);

  std::vector<bool> is_seen(old_num_indexes_ranges, false);
  std::vector<int32*>::const_iterator iter = indexes_ranges_args.begin(),
      end = indexes_ranges_args.end();
  for (; iter != end; ++iter)
    is_seen[**iter] = true;

  std::vector<int32> old_to_new_index(old_num_indexes_ranges);
  typedef std::map<const std::vector<std::pair<int32, int32> >*, int32,
                   PointerCompare<std::pair<int32, int32> > > MapType;
  MapType indexes_map;
  int32 cur_index = 0;
  for (int32 i = 0; i < old_num_indexes_ranges; i++) {
    if (!is_seen[i]) {
      old_to_new_index[i] = -1;
    } else {
      std::pair<MapType::iterator, bool> p =
          indexes_map.insert(
              std::pair<const std::vector<std::pair<int32, int32> >*, int32>(
                  &(computation_->indexes_ranges[i]), cur_index));
      if (p.second) {  // was inserted-- was not there already.
        old_to_new_index[i] = cur_index++;
      } else {
        int32 index_from_map = p.first->second;
        old_to_new_index[i] = index_from_map;
      }
    }
  }
  if (cur_index == old_num_indexes_ranges)
    return;  // An optimization.  No changes to the numbering are made.
  std::vector<std::vector<std::pair<int32, int32> > > new_indexes_ranges(
      cur_index);
  for (int32 i = 0; i < old_num_indexes_ranges; i++) {
    int32 new_index = old_to_new_index[i];
    if (new_index != -1)
      computation_->indexes_ranges[i].swap(new_indexes_ranges[new_index]);
  }
  computation_->indexes_ranges.swap(new_indexes_ranges);

  // renumber the indexes inside the commmands.
  for (iter = indexes_ranges_args.begin(); iter != end; ++iter) {
    int32 old_index = **iter;
    KALDI_ASSERT(old_index >= 0 && old_index < old_num_indexes_ranges);
    int32 new_index = old_to_new_index[old_index];
    KALDI_ASSERT(new_index >= 0);
    **iter = new_index;
  }
}




void RenumberComputation(NnetComputation *computation) {
  ComputationRenumberer renumberer(computation);
  renumberer.Renumber();
}

void RemoveNoOps(NnetComputation *computation) {
  std::vector<NnetComputation::Command>::iterator
      input_iter = computation->commands.begin(),
      input_end = computation->commands.end(),
      output_iter = computation->commands.begin();
  for (; input_iter != input_end; ++input_iter) {
    if (input_iter->command_type != kNoOperation) {
      *output_iter = *input_iter;
      ++output_iter;
    }
  }
  computation->commands.resize(output_iter - computation->commands.begin());
}


VariableMergingOptimizer::VariableMergingOptimizer(
    const NnetOptimizeOptions &config,
    const Nnet &nnet,
    NnetComputation *computation):
    config_(config), nnet_(nnet),
    computation_(computation),
    already_called_merge_variables_(false) {
  analyzer_.Init(nnet, *computation);
  ComputeMatrixToSubmatrix(*computation_, &matrix_to_submatrix_);
  variable_dirty_.resize(analyzer_.variables.NumVariables(), false);
}

bool VariableMergingOptimizer::MergeVariables() {
  KALDI_ASSERT(!already_called_merge_variables_);
  already_called_merge_variables_ = true;
  if (!config_.optimize)
    return false;
  bool merged = false;
  int32 num_commands = computation_->commands.size();
  for (int32 command_index = 0; command_index < num_commands;
       command_index++) {
    // This loop looks for pairs of sub-matrix indexes s1,s2 that we could
    // potentially merge into a single variable.
    const NnetComputation::Command &c =
        computation_->commands[command_index];
    int32 s1 = -1, s2 = -1;
    if (c.command_type == kMatrixCopy &&
        config_.remove_assignments) {
      s2 = c.arg1;  // s2 is the written-to matrix.
      s1 = c.arg2;
    } else if (c.command_type == kPropagate &&
               config_.propagate_in_place) {
      const Component *component = nnet_.GetComponent(c.arg1);
      if (component->Properties() & kPropagateInPlace) {
        s1 = c.arg3;
        s2 = c.arg4;  // s2 is the written-to matrix.
      }
    } else if ((c.command_type == kBackprop ||
                c.command_type == kBackpropNoModelUpdate) &&
               config_.backprop_in_place) {
      const Component *component = nnet_.GetComponent(c.arg1);
      if (component->Properties() & kBackpropInPlace) {
        s1 = c.arg5;
        s2 = c.arg6;  // s2 is the written-to matrix.
        if (s1 == c.arg3 || s2 == c.arg3 || s1 == c.arg4 || s2 == c.arg4) {
          // we don't think this should ever happen, but just out of an
          // abundance of caution: if either of these submatrix indexes are the
          // input-value or output-value args to Backprop, don't do the optimization.
          s1 = -1;
          s2 = -1;
        }
      }
    }
    if (s1 > 0 && s2 > 0) {
      std::pair<bool,bool> p = MayBeMerged(command_index, s1, s2);
      if (p.first) {
        DoMerge(command_index, s1, s2);
        merged = true;
      } else if (p.second) {
        DoMerge(command_index, s2, s1);
        merged = true;
      }
    }
  }
  if (merged) {
    RenumberComputation(computation_);
    RemoveNoOps(computation_);
  }
  return merged;
}

/**
   This static function returns a SubMatrixInfo corresponding to
   replacing the matrix-index in a's "matrix_index" with, essentially, sub-matrix b.
   Of course the matrix_index will be b's "matrix_index", but we may
   have to modify the row and column offsets.  The idea is that sub-matrix
   submat_b should have the same dimensions as the matrix underlying
   submat_a.
 */
static NnetComputation::SubMatrixInfo GetSubMatrixOfSubMatrix(
    const NnetComputation &computation, int32 submat_a, int32 submat_b) {
  KALDI_ASSERT(static_cast<size_t>(submat_a) < computation.submatrices.size());
  KALDI_ASSERT(static_cast<size_t>(submat_b) < computation.submatrices.size());
  const NnetComputation::SubMatrixInfo &a = computation.submatrices[submat_a],
                                       &b = computation.submatrices[submat_b];
  const NnetComputation::MatrixInfo &a_mat =
      computation.matrices[a.matrix_index];
  KALDI_ASSERT(a_mat.num_rows == b.num_rows && a_mat.num_cols == b.num_cols);
  NnetComputation::SubMatrixInfo ans;
  ans.matrix_index = b.matrix_index;
  ans.row_offset = a.row_offset + b.row_offset;
  ans.num_rows = a.num_rows;
  ans.col_offset = a.col_offset + b.col_offset;
  ans.num_cols = a.num_cols;
  return ans;
}

void VariableMergingOptimizer::MarkAsDirty(int32 s) {
  std::vector<int32> variable_indexes;
  analyzer_.variables.AppendVariablesForSubmatrix(s, &variable_indexes);
  std::vector<int32>::const_iterator iter = variable_indexes.begin(),
      end = variable_indexes.end();
  for (; iter != end; ++iter) {
    int32 v = *iter;
    KALDI_ASSERT(static_cast<size_t>(v) < variable_dirty_.size());
    variable_dirty_[v] = true;
  }
}

void VariableMergingOptimizer::DoMerge(int32 command_index,
                                       int32 s_to_keep,
                                       int32 s_to_discard) {
  // Prevent further optimizations touching either submatrix (we can try again
  // in a later round of optimization, with a new instance of this class).
  MarkAsDirty(s_to_keep);
  MarkAsDirty(s_to_discard);

  int32 m_to_keep = computation_->submatrices[s_to_keep].matrix_index,
      m_to_discard = computation_->submatrices[s_to_discard].matrix_index;
  KALDI_ASSERT(m_to_keep != m_to_discard && m_to_keep > 0 && m_to_discard > 0);

  { // modify submatrices of m_to_discard to effectively be sub-matrices of
    // s_to_keep instead (they will refer to m_to_keep as the matrix_index).
    std::vector<int32>::const_iterator iter =
        matrix_to_submatrix_[m_to_discard].begin(),
        end = matrix_to_submatrix_[m_to_discard].end();
    for (; iter != end; ++iter) {
      int32 submatrix_index = *iter;
      KALDI_ASSERT(computation_->submatrices[submatrix_index].matrix_index
                   == m_to_discard);
      computation_->submatrices[submatrix_index] =
          GetSubMatrixOfSubMatrix(*computation_, submatrix_index,
                                  s_to_keep);
    }
  }

  ComputationAnalysis analysis(*computation_, analyzer_);
  NnetComputation::Command &c = computation_->commands[command_index];
  const std::vector<MatrixAccesses> &matrix_accesses =
      analyzer_.matrix_accesses;

  //  - If it was case (a), replace the assignment command with a no-op.
  if (c.command_type == kMatrixCopy) {
    // remove the command.
    c.command_type = kNoOperation;
    c.arg1 = -1;
    c.arg2 = -1;
  }

  //   We want to ensure that there is only one deallocation command.
  //   If neither matrix is an output, then there will be 2 deallocation
  //   commands and we keep the one for m_to_keep (which, if the sizes
  //   differ, will be the larger of the two, so it's the one whose
  //   submatrix index refers to the entirety of the matrix).
  //   If one of them is an output, then remove the deallocation command
  //   of whichever one is not an output.
  //   As a simplification to the logic above: if the 'discard' matrix
  //   has a deallocation command (i.e. if that matrix was not an output)
  //   then remove it; otherwise remove the deallocation command of
  //   the 'keep' matrix.

  int32 dealloc_keep = matrix_accesses[m_to_keep].deallocate_command,
      dealloc_discard = matrix_accesses[m_to_discard].deallocate_command;
  if (dealloc_discard != -1) {
    computation_->commands[dealloc_discard].command_type = kNoOperation;
  } else {
    KALDI_ASSERT(dealloc_keep != -1);
    computation_->commands[dealloc_keep].command_type = kNoOperation;
  }

  {
    //   - Both m_to_keep and m_to_discard will have commands that allocate
    //     them, as all matrices do (note, kAcceptInput counts as an allocation
    //     command).  If one of them is kAcceptInput, then delete the other one.
    //     Otherwise delete the "discard" one.  As a simplification of the logic
    //     of the previous sentence: if the "discard" allocate command is
    //     kAcceptInput then delete the "keep" allocate command, else delete
    //     the "discard" allocate command.
    //     Note: after we renumber the submatrices, they both refer to the
    //     same underlying matrix, but we need to refer to them using a
    //     submatrix that refers to the entire matrix.  The one we keep will
    //     always refer to the entire matrix.  (In the case where one of
    //     them is an input, both submatrices are guaranteed to refer to the
    //     entire matrix).
    int32 alloc_keep = matrix_accesses[m_to_keep].allocate_command,
        alloc_discard = matrix_accesses[m_to_discard].allocate_command;

    KALDI_ASSERT(alloc_keep != -1 && alloc_discard != -1);
    KALDI_ASSERT(analysis.FirstMatrixAccess(m_to_discard) > alloc_keep);

    NnetComputation::Command
        &keep_alloc_command = computation_->commands[alloc_keep],
        &discard_alloc_command = computation_->commands[alloc_discard];
    if (discard_alloc_command.command_type == kAcceptInput) {
      keep_alloc_command.command_type = kNoOperation;
    } else {
      discard_alloc_command.command_type = kNoOperation;
    }
  }

  //  If the matrix to discard had stride_type == kStrideEqualNumCols, set the
  //  matrix to keep's stride_type to kStrideEqualNumCols.
  if (computation_->matrices[m_to_discard].stride_type == kStrideEqualNumCols) {
    computation_->matrices[m_to_keep].stride_type = kStrideEqualNumCols;
    // ... and perform an additional check.
    KALDI_ASSERT(computation_->matrices[m_to_discard].num_rows ==
                 computation_->matrices[m_to_keep].num_rows &&
                 computation_->matrices[m_to_discard].num_cols ==
                 computation_->matrices[m_to_keep].num_cols);
  }
}



std::pair<bool,bool> VariableMergingOptimizer::MayBeMerged(
    int32 command_index, int32 s1, int32 s2) const {
  KALDI_ASSERT(s1 > 0 && s2 > 0 && static_cast<size_t>(command_index) <
               computation_->commands.size());
  if (!config_.allow_left_merge && !config_.allow_right_merge)
    return std::pair<bool,bool>(false,false);
  int32 m1 = computation_->submatrices[s1].matrix_index,
      m2 = computation_->submatrices[s2].matrix_index;
  // we can't merge two different submatrices of the same matrix.
  if (m1 == m2) return std::pair<bool,bool>(false,false);
  std::vector<int32> variable_indexes;
  analyzer_.variables.AppendVariablesForSubmatrix(s1, &variable_indexes);
  analyzer_.variables.AppendVariablesForSubmatrix(s2, &variable_indexes);
  std::vector<int32>::iterator iter = variable_indexes.begin(),
      end = variable_indexes.end();
  // condition c5:
  for (; iter != end; ++iter)
    if (variable_dirty_[*iter])
      return std::pair<bool,bool>(false,false);
  const std::vector<MatrixAccesses> &matrix_accesses = analyzer_.matrix_accesses;
  const MatrixAccesses &m1_access = matrix_accesses[m1],
      &m2_access = matrix_accesses[m2];
  // condition c1:
  if ((m1_access.is_input && m2_access.is_input) ||
      (m1_access.is_output && m2_access.is_output))
    return std::pair<bool,bool>(false,false);
  // condition c2:
  if ((m1_access.is_input || m1_access.is_output ||
       m2_access.is_input || m2_access.is_output) &&
      (!computation_->IsWholeMatrix(s1) ||
       !computation_->IsWholeMatrix(s2)))
    return std::pair<bool,bool>(false,false);
  bool left = config_.allow_left_merge,
      right = config_.allow_right_merge;
  // condition c3:
  if (!computation_->IsWholeMatrix(s2)) left = false;
  // condition c4:
  if (!computation_->IsWholeMatrix(s1)) right = false;
  // condition c6:
  if (computation_->matrices[m2].stride_type == kStrideEqualNumCols &&
      !computation_->IsWholeMatrix(s1)) left = false;
  // condition c7:
  if (computation_->matrices[m1].stride_type == kStrideEqualNumCols &&
      !computation_->IsWholeMatrix(s2)) right = false;


  if (!left && !right)  // save some time.
    return std::pair<bool,bool>(false,false);
  bool is_assignment = (computation_->commands[command_index].command_type ==
                        kMatrixCopy);
  ComputationAnalysis analysis(*computation_, analyzer_);
  if (is_assignment) {
    if (analysis.FirstAccess(s2) == command_index &&
        analysis.LastWriteAccess(s1) < command_index &&
        analysis.LastAccess(s1) <
        analysis.DataInvalidatedCommand(command_index, s2)) {
      return std::pair<bool,bool>(left, right);  // possible success.
    }
  } else {
    if (analysis.FirstAccess(s2) == command_index &&
        analysis.LastAccess(s1) == command_index) {
      return std::pair<bool,bool>(left, right);  // possible success.
    }
  }
  // failure.
  return std::pair<bool,bool>(false,false);
}

void ModelUpdateConsolidator::AppendDebugInfoForSubmatrix(
    int32 submatrix_index,
    NnetComputation::MatrixDebugInfo *debug_info) const {
  KALDI_ASSERT(!computation_->matrix_debug_info.empty());
  KALDI_ASSERT(static_cast<size_t>(submatrix_index) <
               computation_->submatrices.size());
  NnetComputation::SubMatrixInfo submatrix_info =
      computation_->submatrices[submatrix_index];
  int32 matrix_index = submatrix_info.matrix_index;
  KALDI_ASSERT(matrix_index > 0 && static_cast<size_t>(matrix_index) <
               computation_->matrix_debug_info.size());
  const NnetComputation::MatrixDebugInfo &src_info =
      computation_->matrix_debug_info[matrix_index];
  debug_info->is_deriv = src_info.is_deriv;
  KALDI_ASSERT(src_info.cindexes.size() ==
               computation_->matrices[matrix_index].num_rows);
  int32 row_begin = submatrix_info.row_offset,
      row_end = row_begin + submatrix_info.num_rows;
  debug_info->cindexes.insert(debug_info->cindexes.end(),
                             src_info.cindexes.begin() + row_begin,
                             src_info.cindexes.begin() + row_end);
}


// see comment by declaration in header.
int32 ModelUpdateConsolidator::ConsolidateSubmatrices(
    const std::vector<int32> &commands,
    const std::vector<int32> &submatrices) {
  int32 num_submatrices = submatrices.size();
  KALDI_ASSERT(num_submatrices > 1 && commands.size() == submatrices.size());
  int32 first_submatrix = submatrices[0];
  int32 num_cols = computation_->submatrices[first_submatrix].num_cols,
      num_rows = 0;
  MatrixStrideType stride_type = kDefaultStride;
  NnetComputation::MatrixDebugInfo debug_info;
  for (int32 i = 0; i < num_submatrices; i++) {
    int32 submatrix = submatrices[i];
    num_rows += computation_->submatrices[submatrix].num_rows;
    KALDI_ASSERT(computation_->submatrices[submatrix].num_cols == num_cols);
    if (!computation_->matrix_debug_info.empty())
      AppendDebugInfoForSubmatrix(submatrix, &debug_info);
    if (computation_->IsWholeMatrix(submatrix)) {
      int32 matrix = computation_->submatrices[submatrix].matrix_index;
      if (computation_->matrices[matrix].stride_type == kStrideEqualNumCols)
        stride_type = kStrideEqualNumCols;
    }
  }
  // new_whole_submatrix is a new submatrix index corresponding to the whole
  // of a new matrix that we are creating.
  int32 new_whole_submatrix = computation_->NewMatrix(num_rows, num_cols,
                                                      stride_type);
  // Add a command at the very start, to initialize this new matrix.
  // we can later on optimize this zeroed initialization to an undefined
  // initialization.
  extra_commands_[0].push_back(
      NnetComputation::Command(kAllocMatrixZeroed, new_whole_submatrix));
  final_deallocate_commands_.push_back(
      NnetComputation::Command(kDeallocMatrix, new_whole_submatrix));
  int32 new_matrix_index =
      computation_->submatrices[new_whole_submatrix].matrix_index;
  if (!computation_->matrix_debug_info.empty())
    computation_->matrix_debug_info[new_matrix_index].Swap(&debug_info);

  int32 row_offset = 0;
  for (int32 i = 0; i < num_submatrices; i++) {
    int32 submatrix_index = submatrices[i];
    int32 this_num_rows = computation_->submatrices[submatrix_index].num_rows;
    // submatrix corresponding to the part of the new matrix corresponding
    // to 'submatrices[i]'.
    int32 new_submatrix = computation_->NewSubMatrix(new_whole_submatrix,
                                                     row_offset, this_num_rows,
                                                     0, num_cols);
    // Just before command 'commands[i]', add a command that assigns to the
    // submatrix numbered 'new_submatrix' the contents of the submatrix numbered
    // 'submatrices[i]'.  Note: we hope that a later pass of optimization
    // (VariableMergingOptimization) will remove this redundant copy by
    // having the operation that created it write directly to the location
    // we want it to be.
    NnetComputation::Command c(kMatrixCopy, new_submatrix, submatrices[i]);
    extra_commands_[commands[i]].push_back(c);
    row_offset += this_num_rows;
  }
  KALDI_ASSERT(row_offset == num_rows);
  return new_whole_submatrix;
}

void ModelUpdateConsolidator::AddCommandsToComputation() {
  KALDI_ASSERT(computation_->commands.size() == extra_commands_.size());
  int32 old_num_commands = computation_->commands.size(),
      new_num_commands = old_num_commands +
      static_cast<int32>(final_commands_.size() +
                         final_deallocate_commands_.size());
  for (size_t i = 0; i < extra_commands_.size(); i++)
    new_num_commands += static_cast<int32>(extra_commands_[i].size());
  std::vector<NnetComputation::Command> new_commands;
  new_commands.reserve(new_num_commands);
  for (int32 c = 0; c < old_num_commands; c++) {
    new_commands.insert(new_commands.end(),
                        extra_commands_[c].begin(), extra_commands_[c].end());
    new_commands.push_back(computation_->commands[c]);
  }
  new_commands.insert(new_commands.end(),
                      final_commands_.begin(), final_commands_.end());
  new_commands.insert(new_commands.end(),
                      final_deallocate_commands_.begin(),
                      final_deallocate_commands_.end());
  computation_->commands.swap(new_commands);
}

/** This function, called from ConsolidateModelUpdate, is passed a list of
    commands that are all backprops for the same component, and it consolidates
    them into a single model-update command. */
void ModelUpdateConsolidator::ConsolidateUpdateForComponent(
    int32 component_index,
    const std::vector<int32> &backprop_commands) {
  const Component *component = nnet_.GetComponent(component_index);
  int32 num_backprop_commands = backprop_commands.size();

  bool need_input = (component->Properties() & kBackpropNeedsInput) != 0,
      need_output = (component->Properties() & kBackpropNeedsOutput) != 0;

  std::vector<int32>  input_submatrices(num_backprop_commands),
      output_submatrices(num_backprop_commands),
      output_deriv_submatrices(num_backprop_commands);

  for (int32 i = 0; i < num_backprop_commands; i++) {
    int32 command_index = backprop_commands[i];
    NnetComputation::Command &command =
        computation_->commands[command_index];
    // arg2 must be 0 because simple components don't use precomputed indexes.
    KALDI_ASSERT(command.command_type == kBackprop && command.arg2 == 0);
    command.command_type = kBackpropNoModelUpdate;
    int32 input_submatrix = command.arg3,
        output_submatrix = command.arg4,
        output_deriv_submatrix = command.arg5;
    KALDI_ASSERT((input_submatrix != 0) == need_input &&
                 (output_submatrix != 0) == need_output);
    input_submatrices[i] = input_submatrix;
    output_submatrices[i] = output_submatrix;
    output_deriv_submatrices[i] = output_deriv_submatrix;
  }
  // Get the sub-matrix indexes of whichever of the consolidated matrices we
  // need (will usually be input_submatrix and output_deriv_submatrix).
  int32 input_submatrix = (need_input ?
                           ConsolidateSubmatrices(backprop_commands,
                                                  input_submatrices) : 0),
      output_submatrix = (need_output ?
                         ConsolidateSubmatrices(backprop_commands,
                                                output_submatrices) : 0),
      output_deriv_submatrix = ConsolidateSubmatrices(backprop_commands,
                                                      output_deriv_submatrices);
  int32 precomputed_indexes_index = 0,  // unused since simple component
      input_deriv_submatrix = 0;  // we don't need the input-deriv, so this is
                                  // zero.
  NnetComputation::Command c(kBackprop, component_index, precomputed_indexes_index,
                             input_submatrix, output_submatrix,
                             output_deriv_submatrix, input_deriv_submatrix);
  final_commands_.push_back(c);
}

ModelUpdateConsolidator::ModelUpdateConsolidator(
    const Nnet &nnet,
    NnetComputation *computation):
    nnet_(nnet), computation_(computation),
    extra_commands_(computation->commands.size()) { }

void ModelUpdateConsolidator::ConsolidateModelUpdate() {
  int32 num_components = nnet_.NumComponents(),
      num_commands = computation_->commands.size();
  // 'backprop_commands' is a list, for each component (but nonempty only for
  // updatable components), of the command indexes for the backprop commands.
  std::vector<std::vector<int32> > backprop_commands(num_components);
  for (int32 command_index = 0;
       command_index < num_commands; command_index++) {
    const NnetComputation::Command &c = computation_->commands[command_index];
    if (c.command_type == kBackprop) {
      int32 component_index = c.arg1;
      const Component *component = nnet_.GetComponent(component_index);
      if (component->Properties() & kUpdatableComponent)
        backprop_commands[component_index].push_back(command_index);
    }
  }
  bool consolidated = false;
  for (int32 component = 0; component < num_components; component++) {
    if (backprop_commands[component].size() > 1) {
      ConsolidateUpdateForComponent(component,
                                    backprop_commands[component]);
      consolidated = true;
    }
  }
  if (!consolidated)  // This is an optimization to avoid redundant computation
    return;           // if there is nothing to do.
  // the following function call commits all the commands we stored in member
  // variables, to computation_->commands.
  AddCommandsToComputation();
}

// inline
void DerivativeTimeLimiter::GetPruneValues(int32 initial_submatrix,
                                           int32 new_submatrix,
                                           int32 *left_prune,
                                           int32 *right_prune) const {
  KALDI_ASSERT(initial_submatrix > 0 && new_submatrix > 0);
  const NnetComputation::SubMatrixInfo
      initial_info = computation_->submatrices[initial_submatrix],
      new_info = computation_->submatrices[new_submatrix];
  KALDI_ASSERT(initial_info.matrix_index == new_info.matrix_index);
  *left_prune = new_info.row_offset - initial_info.row_offset;
  if (right_prune != NULL) {
    *right_prune = initial_info.num_rows - new_info.num_rows - *left_prune;
    KALDI_ASSERT(*left_prune >= 0 && *right_prune >= 0);
  }
}

// modify commands to take into account the fact that some matrices are zero or
// partially zero.  Allocation commands and sizes of underlying matrices are not
// affected-- we'll work out later on, what to do with them.
void DerivativeTimeLimiter::ModifyCommand(NnetComputation::Command *command) {
  CommandType command_type = command->command_type;
  switch (command_type) {
    case kAllocMatrixUndefined:
    case kAllocMatrixFromOther:
    case kAllocMatrixFromOtherZeroed:
      KALDI_ERR << "No undefined initialization or initialization-from-other "
                << "is allowed before LimitDerivativeTimes";
      break;
    case kAllocMatrixZeroed:
    case kDeallocMatrix:
      break;  // we'll deal with allocation and deallocation later on.
    case kPropagate:
      // Propagate commands are unchanged.
      break;
    case kStoreStats: {
      const Component *component = nnet_.GetComponent(command->arg1);
      if ((component->Properties() & kSimpleComponent)) {
        // We choose to apply the time-limitation here, as it will save time and
        // is probably what the user wants.
        int32 submatrix_mapped = submatrix_map_[command->arg2];
        if (submatrix_mapped == 0)
          command->command_type = kNoOperation;
        else
          command->arg2 = submatrix_mapped;
      }
      break;
    }
    case kBackpropNoModelUpdate:  // we actually don't expect to encounter this,
                                  // but it's trivial to support as it's the
                                  // same as backprop.
    case kBackprop: {
      const Component *component = nnet_.GetComponent(command->arg1);
      if (!(component->Properties() & kSimpleComponent)) {
        // we don't (yet) do this optimization for non-simple Components...
        // it would be a bit more complicated as we'd have to recompute the
        // PrecomputedIndexes.
        break;
      }
      int32 input_submatrix = command->arg3,
          output_submatrix = command->arg4,
          output_deriv_submatrix = command->arg5,
          input_deriv_submatrix = command->arg6;
      int32 mapped_input_submatrix = submatrix_map_[input_submatrix],
           mapped_output_submatrix =  submatrix_map_[output_submatrix],
     mapped_output_deriv_submatrix = submatrix_map_[output_deriv_submatrix],
      mapped_input_deriv_submatrix = submatrix_map_[input_deriv_submatrix];

      if (mapped_output_deriv_submatrix == 0) {
        // completely outside range..
        KALDI_ASSERT(mapped_input_deriv_submatrix == 0 &&
                     mapped_input_submatrix == 0 &&
                     mapped_output_submatrix == 0);
        // just delete the command.
        command->command_type = kNoOperation;
      } else if (mapped_output_deriv_submatrix !=
                 output_deriv_submatrix) {
        // we're operating on a range of the input or output.
        command->arg3 = mapped_input_submatrix;
        command->arg4 = mapped_output_submatrix;
        command->arg5 = mapped_output_deriv_submatrix;
        command->arg6 = mapped_input_deriv_submatrix;
      }
      break;
    }
    case kMatrixCopy: case kMatrixAdd:
      MapSimpleMatrixCommand(command);
      break;
    case kCopyRows: case kAddRows:
      MapIndexesCommand(command);
      break;
    case kCopyRowsMulti: case kCopyToRowsMulti:
    case kAddRowsMulti: case kAddToRowsMulti:
      MapIndexesMultiCommand(command);
      break;
    case kAddRowRanges: {
      MapAddRowRangesCommand(command);
      break;
    }
    case kAcceptInput: case kProvideOutput:
    case kNoOperation: case kNoOperationMarker:
      break;
    default:
      KALDI_ERR << "Un-handled command type.";
  }
}

void DerivativeTimeLimiter::MapSimpleMatrixCommand(NnetComputation::Command *c) {
  int32 submatrix1 = c->arg1,
      submatrix2 = c->arg2;
  int32 submatrix1_mapped = submatrix_map_if_deriv_[submatrix1],
      submatrix2_mapped = submatrix_map_if_deriv_[submatrix2];
  if (submatrix1_mapped == submatrix1 &&
      submatrix2_mapped == submatrix2) {
    // nothing to do.
    return;
  }
  if (submatrix1_mapped == 0 || submatrix2_mapped == 0) {
    // remove the operation-- it has nothing to do.
    c->command_type = kNoOperation;
    return;
  }
  // left_prune1 is the number of rows pruned away on the left for submatrix1.
  int32 orig_num_rows = computation_->submatrices[submatrix1].num_rows,
      left_prune1, left_prune2, right_prune1, right_prune2;
  GetPruneValues(submatrix1, submatrix1_mapped, &left_prune1, &right_prune1);
  GetPruneValues(submatrix2, submatrix2_mapped, &left_prune2, &right_prune2);
  if (left_prune1 == left_prune2 && right_prune1 == right_prune2) {
    // we took the same number of rows away from the left and right for
    // both arguments; the normal mapped values will work in this case
    c->arg1 = submatrix1_mapped;
    c->arg2 = submatrix2_mapped;
  } else {
    // there is some kind of mismatch- we'll prune back to what remains
    // after applying the maximum pruning on the left and right.
    int32 left_prune = std::max(left_prune1, left_prune2),
        right_prune = std::max(right_prune1, right_prune2);
    if (left_prune + right_prune >= orig_num_rows) {
      // everything was pruned away; remove the operation.
      c->command_type = kNoOperation;
      return;
    } else {
      int32 num_rows = orig_num_rows - left_prune - right_prune;
      // note: the call NewSubMatrix effectively gives us a sub-matrix of a
      // sub-matrix.
      c->arg1 = computation_->NewSubMatrix(submatrix1,
                                           left_prune, num_rows, 0, -1);
      c->arg2 = computation_->NewSubMatrix(submatrix2,
                                           left_prune, num_rows, 0, -1);
    }
  }
}

// does the processing for a command of type kCopyRows or kAddRows, where
// 1st and 2nd args are submatrix indexes and the 3rd arg is a vector of
// row-indexes.
void DerivativeTimeLimiter::MapIndexesCommand(NnetComputation::Command *c) {
  int32 output_submatrix = c->arg1,
      input_submatrix = c->arg2;
  int32 input_submatrix_mapped = submatrix_map_if_deriv_[input_submatrix],
      output_submatrix_mapped = submatrix_map_if_deriv_[output_submatrix];
  // input_submatrix_mapped and output_submatrix_mapped map both submatrices to
  // just the portion that we are treating as nonzero.

  if (input_submatrix_mapped == input_submatrix &&
      output_submatrix_mapped == output_submatrix) {
    return;  // nothing is changed.
  }
  if (input_submatrix_mapped == 0 ||
      output_submatrix_mapped == 0) {
    // Either input or output is all zeros; make the command a no-op.
    // It may not be obvious that in the case of kCopyRows it would
    // be valid to make this a no-op (because what if the existing
    // contents were nonzero?), but we insist that this optimization
    // come before optimizations, and we know that the originally
    // generated computation would not overwrite a nonzero value
    // (and there are no undefined values because we make sure to
    // initialize everything with zeros; ununitialized values are
    // allowed only at a later optimization stage.
    c->command_type = kNoOperation;
    return;
  }
  const std::vector<int32> &old_indexes = computation_->indexes[c->arg3];

  int32 left_prune_input, left_prune_output;
  GetPruneValues(input_submatrix, input_submatrix_mapped,
                 &left_prune_input, NULL);
  GetPruneValues(output_submatrix, output_submatrix_mapped,
                 &left_prune_output, NULL);
  int32 new_num_input_rows =
      computation_->submatrices[input_submatrix_mapped].num_rows,
      new_num_output_rows =
      computation_->submatrices[output_submatrix_mapped].num_rows;
  std::vector<int32> new_indexes(new_num_output_rows);
  bool must_keep_command = false;
  for (int32 i = 0; i < new_num_output_rows; i++) {
    // the index into the 'new_indexes' vector is the row of the output
    // submatrix; the value is the row of the input submatrix.
    int32 orig_index = old_indexes[i + left_prune_output];
    if (orig_index == -1) {
      new_indexes[i] = -1;
    } else {
      int32 mapped_index = orig_index - left_prune_input;
      if (mapped_index >= 0 && mapped_index < new_num_input_rows) {
        new_indexes[i] = mapped_index;
        must_keep_command = true;
      } else {
        // input was out of range (i.e. it takes a value that we are asserting
        // is zero)-- use -1 as the index.
        new_indexes[i] = -1;
      }
    }
  }
  if (!must_keep_command) {
    c->command_type = kNoOperation;
    return;
  }
  int32 new_indexes_index = computation_->indexes.size();
  computation_->indexes.push_back(new_indexes);
  c->arg1 = output_submatrix_mapped;
  c->arg2 = input_submatrix_mapped;
  c->arg3 = new_indexes_index;
}

void DerivativeTimeLimiter::MapIndexesMultiCommand(NnetComputation::Command *c) {
  int32 submatrix_arg = c->arg1,
      indexes_multi_arg = c->arg2;
  int32 submatrix_mapped = submatrix_map_if_deriv_[submatrix_arg];
  if (submatrix_mapped == 0) {
    c->command_type = kNoOperation;
    return;
  }
  int32 left_prune;
  GetPruneValues(submatrix_arg, submatrix_mapped, &left_prune, NULL);
  int32 new_num_rows = computation_->submatrices[submatrix_mapped].num_rows;
  const std::vector<std::pair<int32, int32> > &old_indexes_multi(
      computation_->indexes_multi[indexes_multi_arg]);
  std::vector<std::pair<int32, int32> > new_indexes_multi(new_num_rows);
  for (int32 i = 0; i < new_num_rows; i++) {
    std::pair<int32,int32> &this_pair = new_indexes_multi[i];
    this_pair = old_indexes_multi[i + left_prune];
    int32 this_submatrix = this_pair.first,
        this_row = this_pair.second;
    if (this_submatrix == -1)  // don't map the (-1, -1) pairs.
      continue;
    int32 this_submatrix_mapped = submatrix_map_if_deriv_[this_submatrix];
    if (this_submatrix_mapped == this_submatrix) {
      continue;
    } else if (this_submatrix_mapped == 0) {  // was completely out of range.
      this_pair.first = -1;
      this_pair.second = -1;
    } else {
      int32 this_left_prune, this_num_rows =
          computation_->submatrices[this_submatrix_mapped].num_rows;
      GetPruneValues(this_submatrix, this_submatrix_mapped,
                     &this_left_prune, NULL);
      int32 this_row_mapped = this_row - this_left_prune;
      if (this_row_mapped >= 0 && this_row_mapped < this_num_rows) {
        this_pair.first = this_submatrix_mapped;
        this_pair.second = this_row_mapped;
      } else {
        this_pair.first = -1;
        this_pair.second = -1;
      }
    }
  }
  if (submatrix_mapped == submatrix_arg &&
      new_indexes_multi == old_indexes_multi)  // nothing changed.
    return;
  bool command_can_be_deleted = true;
  std::vector<std::pair<int32, int32> >::iterator
      iter = new_indexes_multi.begin(),
      end = new_indexes_multi.end();
  for (; iter != end; ++iter) {
    if (iter->first != -1) {
      command_can_be_deleted = false;
      break;
    }
  }
  if (command_can_be_deleted) {
    c->command_type = kNoOperation;
    return;
  }
  c->arg1 = submatrix_mapped;
  c->arg2 = computation_->indexes_multi.size();
  computation_->indexes_multi.push_back(new_indexes_multi);
}

void DerivativeTimeLimiter::MapAddRowRangesCommand(
    NnetComputation::Command *c) {
  int32 dest_submatrix = c->arg1,
      src_submatrix = c->arg2,
      indexes_ranges_index = c->arg3;
  int32 dest_submatrix_mapped = submatrix_map_if_deriv_[dest_submatrix],
      src_submatrix_mapped = submatrix_map_if_deriv_[src_submatrix];
  if (dest_submatrix_mapped == dest_submatrix &&
      src_submatrix_mapped == src_submatrix)
    return;
  if (dest_submatrix_mapped == 0 || src_submatrix_mapped == 0) {
    c->command_type = kNoOperation;
    return;
  }
  int32 dest_num_rows = computation_->submatrices[dest_submatrix_mapped].num_rows,
      src_num_rows = computation_->submatrices[src_submatrix_mapped].num_rows,
      src_left_prune, dest_left_prune;
  GetPruneValues(dest_submatrix, dest_submatrix_mapped,
                 &dest_left_prune, NULL);
  GetPruneValues(src_submatrix, src_submatrix_mapped,
                 &src_left_prune, NULL);
  const std::vector<std::pair<int32,int32> > &old_indexes_ranges(
      computation_->indexes_ranges[indexes_ranges_index]);
  std::vector<std::pair<int32,int32> > new_indexes_ranges(dest_num_rows);
  for (int32 i = 0; i < dest_num_rows; i++) {
    std::pair<int32, int32> &this_pair = new_indexes_ranges[i];
    this_pair = old_indexes_ranges[i + dest_left_prune];
    // note: the .first is a start-index in the src matrix, and the .second is
    // an end-index in the src matrix.
    int32 new_first = this_pair.first - src_left_prune,
        new_second = this_pair.second - src_left_prune;
    if (new_first < 0) new_first = 0;
    if (new_first >= src_num_rows) new_first = src_num_rows - 1;
    if (new_second < 0) new_second = 0;
    if (new_second >= src_num_rows) new_second = src_num_rows - 1;
    if (new_first == new_second) {
      // for clarity, represent empty ranges as (-1, -1).
      new_first = -1;
      new_second = -1;
    }
    KALDI_ASSERT(new_second >= new_first);
    this_pair.first = new_first;
    this_pair.second = new_second;
  }
  c->arg1 = dest_submatrix_mapped;
  c->arg2 = src_submatrix_mapped;
  c->arg3 = computation_->indexes_ranges.size();
  computation_->indexes_ranges.push_back(new_indexes_ranges);
}


DerivativeTimeLimiter::DerivativeTimeLimiter(const Nnet &nnet,
                                             int32 min_deriv_time,
                                             int32 max_deriv_time,
                                             NnetComputation *computation):
    nnet_(nnet),
    min_deriv_time_(min_deriv_time),
    max_deriv_time_(max_deriv_time),
    computation_(computation) { }

void DerivativeTimeLimiter::LimitDerivTimes() {
  KALDI_ASSERT(max_deriv_time_ >= min_deriv_time_);
  if (min_deriv_time_ == std::numeric_limits<BaseFloat>::min() &&
      max_deriv_time_ == std::numeric_limits<BaseFloat>::max())
    return;  // nothing to do.

  computation_->GetWholeSubmatrices(&whole_submatrices_);
  ComputeMatrixPruneInfo();
  ComputeSubmatrixMaps();
  ModifyCommands();
  PruneMatrices();
  RemoveNoOps(computation_);
  RenumberComputation(computation_);
}

void DerivativeTimeLimiter::ComputeMatrixPruneInfo() {
  KALDI_ASSERT(computation_->matrix_debug_info.size() ==
               computation_->matrices.size() &&
               "Limiting derivative times requires debug info.");
  const int32 num_matrices = computation_->matrices.size(),
      min_deriv_time = min_deriv_time_,
      max_deriv_time = max_deriv_time_;
  matrix_prune_info_.resize(num_matrices);
  // matrix_prune_info_[0] will remain undefined.
  for (int32 matrix_index = 1; matrix_index < num_matrices; matrix_index++) {
    NnetComputation::MatrixDebugInfo &debug_info =
        computation_->matrix_debug_info[matrix_index];
    MatrixPruneInfo &prune_info = matrix_prune_info_[matrix_index];
    const std::vector<Cindex> &cindexes = debug_info.cindexes;
    int32 num_rows = computation_->matrices[matrix_index].num_rows;
    KALDI_ASSERT(num_rows == static_cast<int32>(cindexes.size()));
    int32 first_row_within_range = num_rows,
        last_row_within_range = -1;
    for (int32 i = 0; i < num_rows; i++) {
      int32 t = cindexes[i].second.t;
      if (t >= min_deriv_time && t <= max_deriv_time) {
        if (i < first_row_within_range) first_row_within_range = i;
        if (i > last_row_within_range) last_row_within_range = i;
      }
    }
    if (last_row_within_range == -1) {
      prune_info.fully_inside_range = false;
      prune_info.partly_inside_range = false;
    } else if (last_row_within_range == num_rows - 1 &&
               first_row_within_range == 0) {
      prune_info.fully_inside_range = true;
      prune_info.partly_inside_range = false;
    } else {
      prune_info.fully_inside_range = false;
      prune_info.partly_inside_range = true;
      prune_info.row_begin = first_row_within_range;
      prune_info.row_end = last_row_within_range + 1;
    }
  }
}

void DerivativeTimeLimiter::ComputeSubmatrixMaps() {
  int32 num_submatrices = computation_->submatrices.size();
  submatrix_map_.resize(num_submatrices);
  submatrix_map_if_deriv_.resize(num_submatrices);
  // index zero is for the empty submatrix.
  submatrix_map_[0] = 0;
  submatrix_map_if_deriv_[0] = 0;
  for (int32 s = 1; s < num_submatrices; s++) {
    NnetComputation::SubMatrixInfo &submatrix_info(computation_->submatrices[s]);
    int32 matrix_index = submatrix_info.matrix_index;
    int32 row_offset = submatrix_info.row_offset,
        num_rows = submatrix_info.num_rows;
    const MatrixPruneInfo &matrix_prune_info = matrix_prune_info_[matrix_index];
    if (matrix_prune_info.fully_inside_range) {
      submatrix_map_[s] = s;
    } else if (!matrix_prune_info.partly_inside_range) {
      // completely outside time range.
      submatrix_map_[s] = 0;
    } else {
      // the matrix is partly inside the time range.
      int32 pruned_row_begin = std::max(matrix_prune_info.row_begin,
                                        row_offset),
          pruned_row_end = std::min(matrix_prune_info.row_end,
                                    row_offset + num_rows);
      if (pruned_row_end <= pruned_row_begin) {
        // there was no overlap between the submatrix and the part
        // of the matrix that was inside the time range.
        submatrix_map_[s] = 0;
      } else {
        // caution: this invalidates the reference 'submatrix_info'.
        int32 row_offset_within_submatrix =
            pruned_row_begin - row_offset,
            new_num_rows = pruned_row_end - pruned_row_begin;
        submatrix_map_[s] =
            computation_->NewSubMatrix(s, row_offset_within_submatrix,
                                       new_num_rows, 0, -1);
      }
    }
    bool is_deriv = computation_->matrix_debug_info[matrix_index].is_deriv;
    submatrix_map_if_deriv_[s] = (is_deriv ?
                                  submatrix_map_[s] : s);
  }
}

void DerivativeTimeLimiter::ModifyCommands() {
  std::vector<NnetComputation::Command>::iterator
      iter = computation_->commands.begin(),
      end =  computation_->commands.end();
  for (; iter != end; ++iter)
    ModifyCommand(&(*iter));
}

// called from PruneMatrices only for matrices that are derivatives,
// not inputs or outputs of the computation, and which are partly
// inside the time range, this function returns true if we can
// limit the size of the matrix (because variables outside the
// desired range are never accessed), and false otherwise.
bool DerivativeTimeLimiter::CanLimitMatrix(const Analyzer &analyzer,
                                           int32 m) const {
  int32 s_whole = whole_submatrices_[m];  // submatrix consisting of
                                                     // all of the matrix.
  int32 s_mapped = submatrix_map_[s_whole];  // the matrix limited in time.
  KALDI_ASSERT(s_mapped != 0 && s_mapped != s_whole);
  std::vector<int32> whole_variables, mapped_variables;
  analyzer.variables.AppendVariablesForSubmatrix(s_whole,
                                                 &whole_variables);
  analyzer.variables.AppendVariablesForSubmatrix(s_mapped,
                                                 &mapped_variables);
  KALDI_ASSERT(whole_variables.size() > mapped_variables.size());
  std::vector<int32> excluded_variables(whole_variables.size() -
                                        mapped_variables.size());
  std::vector<int32>::iterator end_iter =
      std::set_difference(whole_variables.begin(), whole_variables.end(),
                          mapped_variables.begin(), mapped_variables.end(),
                          excluded_variables.begin());
  KALDI_ASSERT(end_iter == excluded_variables.end());
  // We want to make sure that none of the excluded variables are
  // ever accessed.  If they are, we cannot prune the matrix.
  int32 allocate_command = analyzer.matrix_accesses[m].allocate_command;
  for (std::vector<int32>::iterator iter = excluded_variables.begin();
       iter != end_iter; ++iter) {
    int32 variable_index = *iter;
    const std::vector<Access> &variable_accesses =
        analyzer.variable_accesses[variable_index];
    std::vector<Access>::const_iterator viter = variable_accesses.begin(),
        vend = variable_accesses.end();
    for (; viter != vend; ++viter) {
      // if a variable outside the pruned range of the matrix is ever accessed
      // apart from on allocation, we cannot prune.
      if (viter->command_index != allocate_command) {
        // we may one day want to look at this.. it's not really expected.
        KALDI_VLOG(4) << "Cannot prune matrix " << m;
        return false;
      }
    }
  }
  return true;
}

void DerivativeTimeLimiter::LimitMatrices(const std::vector<bool> &will_limit) {
  // first modify 'submatrices'.
  int32 num_submatrices = computation_->submatrices.size(),
      num_matrices = computation_->matrices.size();
  for (int32 s = 1; s < num_submatrices; s++) {
    NnetComputation::SubMatrixInfo &submat_info = computation_->submatrices[s];
    int32 m = submat_info.matrix_index;
    if (will_limit[m]) {
      // we need to do something...
      const MatrixPruneInfo &prune_info = matrix_prune_info_[m];
      int32 matrix_num_rows = prune_info.row_end - prune_info.row_begin;
      KALDI_ASSERT(matrix_num_rows > 0 &&
                   matrix_num_rows < computation_->matrices[m].num_rows);
      KALDI_ASSERT(prune_info.partly_inside_range);
      int32 new_row_begin = submat_info.row_offset - prune_info.row_begin;
      if (new_row_begin >= 0 &&
          submat_info.num_rows + new_row_begin <= matrix_num_rows) {
        // If this submatrix is entirely inside the limited range of the matrix,
        // then we modify its row_offset to account for the truncation of
        // rows to the left.
        submat_info.row_offset = new_row_begin;
      } else {
        // This submatrix is not entirely inside the kept range of the matrix.
        // We assume that this submatrix is never accessed directly except (if
        // it was the whole matrix) for in allocation and deallocation commands,
        // since when we modified the computation we ensured this.
        if (computation_->IsWholeMatrix(s)) {
          // If it was the whole matrix then it may be used in allocation and
          // deallocation commands, so we should modify it to be the whole of the
          // new matrix, which will have fewer rows than before.
          submat_info.num_rows = matrix_num_rows;
        } else {
          // We believe this matrix should never be used.  We give it a valid
          // but stupid size of num-rows=1, num-cols=1, so that if it ever does
          // get accessed it should produce an error.
          submat_info.row_offset = 0;
          submat_info.num_rows = 1;
          submat_info.col_offset = 0;
          submat_info.num_cols = 1;
        }
      }
    }
  }
  // next modify 'matrices'
  for (int32 m = 1; m < num_matrices; m++) {
    if (will_limit[m]) {
      const MatrixPruneInfo &prune_info = matrix_prune_info_[m];
      NnetComputation::MatrixInfo &matrix_info = computation_->matrices[m];
      if (!computation_->matrix_debug_info.empty()) {
        NnetComputation::MatrixDebugInfo &debug_info =
            computation_->matrix_debug_info[m];
        std::vector<Cindex> &cindexes = debug_info.cindexes;
        KALDI_ASSERT(cindexes.size() == static_cast<size_t>(matrix_info.num_rows));
        cindexes.erase(cindexes.begin() + prune_info.row_end, cindexes.end());
        cindexes.erase(cindexes.begin(),
                       cindexes.begin() + prune_info.row_begin);
      }
      matrix_info.num_rows = prune_info.row_end - prune_info.row_begin;
      // num_cols stays the same.
    }
  }
}

void DerivativeTimeLimiter::PruneMatrices() {
  Analyzer analyzer;
  analyzer.Init(nnet_, *computation_);
  KALDI_ASSERT(computation_->matrices.size() == whole_submatrices_.size());
  int32 num_matrices = computation_->matrices.size();
  std::vector<bool> will_limit(num_matrices, false);
  bool will_limit_at_least_one = false;
  for (int32 m = 1; m < num_matrices; m++) {
    const MatrixAccesses &accesses = analyzer.matrix_accesses[m];
    const MatrixPruneInfo &matrix_prune_info = matrix_prune_info_[m];
    if (matrix_prune_info.fully_inside_range ||
        accesses.is_input || accesses.is_output ||
        !computation_->matrix_debug_info[m].is_deriv)
      continue;  // nothing to do: it's inside the time-range or not a
                 // derivative.
    // if we got here it's not completely inside the time range, not an input or
    // an output, and it's a derivative.
    if (!matrix_prune_info.partly_inside_range) {
      // completely outside time range.  we can prune the matrix if it is not an
      // input or output, and is never accessed apart from allocation.
      if (accesses.accesses.empty() ||
          (accesses.accesses.size() == 1 &&
           accesses.accesses[0].command_index == accesses.allocate_command)) {
        // we prune the matrix away.  the only thing we need to do here is
        // to remove the allocation and deallocation commands.
        // they should exist, because we just checked that it's not an input
        // or an output.
        KALDI_ASSERT(accesses.allocate_command >= 0 &&
                     accesses.deallocate_command >= 0);
        computation_->commands[accesses.allocate_command].command_type =
            kNoOperation;
        computation_->commands[accesses.deallocate_command].command_type =
            kNoOperation;
      }
    } else {
      // the matrix is partly inside the time range, it's a derivative, and not
      // an input or an output.
      if (CanLimitMatrix(analyzer, m)) {
        will_limit[m] = true;
        will_limit_at_least_one = true;
      }
    }
  }
  if (will_limit_at_least_one)
    LimitMatrices(will_limit);
}

void LimitDerivativeTimes(const Nnet &nnet,
                          int32 min_deriv_time,
                          int32 max_deriv_time,
                          NnetComputation *computation) {
  DerivativeTimeLimiter limiter(nnet, min_deriv_time, max_deriv_time,
                                computation);
  limiter.LimitDerivTimes();
}


class ComputationLoopedOptimizer {
 public:
  ComputationLoopedOptimizer(const Nnet &nnet,
                             NnetComputation *computation):
      nnet_(nnet), computation_(computation) { }
  bool Optimize();

 private:

  // Figures out the time shift between the successive computation requests.
  static int32 FindTimeShift(const NnetComputation &computation,
                             const std::vector<int32> &segment_ends);

  // This function creates a mapping from a matrix-index > 0,
  // to a pair (unique_id, time_offset) that represents the debug-info
  // for that matrix-id in computation.debug_info.
  // The output vector is indexed by the matrix-index in the computation (the
  // zeroth member is not valid).  It requires that the
  // The 'time_offset' is equal to the 't' value of the zeroth element of the
  // cindexes vetor.  The 'unique_id' is an integer that uniquely identifies
  // what we get from subtracting the 'time_offset' from each 't' value of
  // that 'cindexes' vector, and then pairing it up with the 'is_deriv'
  // value of the DebugInfo.  That is, if two 'cindexes' vectors differ only
  // by a time offset, and the 'is_deriv' values are the same they will map to the same
  // unique_id.
  // The output 'matrix_to_pair' is indexed by matrix index (the zeroth element is
  // not set).
  static void CreateMatrixPairs(const NnetComputation &computation,
                                std::vector<std::pair<int32, int32> > *matrix_to_pair);


  // This very simple helper function reverses the map 'matrix_to_pair' so we can
  // do the reverse lookup.  It outputs a map from pair to matrix index m, where
  // 1 <= m < matrix_to_pair.size().
  static void GetPairToMatrixMap(
      std::vector<std::pair<int32, int32> > &matrix_to_pair,
      unordered_map<std::pair<int32, int32>, int32, PairHasher<int32> > *pair_to_matrix);


  // Given a vector of lists, one list for each segment, of the active matrices
  // at the end of that segment, this function converts those lists into a
  // different representation where each matrix is reprented as a pair instead
  // of as a single int32.  'active_pairs' will have the same dimensions as
  // 'active_matrices'.
  static void ConvertListsToPairLists(
      const std::vector<std::vector<int32> > &active_matrices,
      const std::vector<std::pair<int32, int32> > &matrix_to_pair,
      std::vector<std::vector<std::pair<int32, int32> > > *active_pairs);

  // This function modifies the lists of active matrices per segment
  // (represented as pairs) in 'active_pairs' by sorting them and
  // then subtracting the time-offset of the first pair in each
  // list ((*active_pair)[seg][0].second), from all elements in that list.
  // It puts the subtracted offset in (*time_offsets)[seg].  This change
  // of representation makes it easy to tell whether the sets of active
  // matrices for different segments are identical up to a time-offset.
  static void NormalizePairLists(
      std::vector<std::vector<std::pair<int32, int32> > > *active_pairs,
      std::vector<int32> *time_offsets);

  // This function looks in the matrix 'active_pairs' for the first pair of
  // identical values, i.e. it is looking for i < j for which
  // normalized_active_pairs[i] == normalized_active_pairs[j].  (However, the
  // pair i,j must satisfy an extra condition, see below).  If a pair
  // i,j exists satisfying these conditions, this function outputs them to *seg1
  // and *seg2, and returns true; otherwise it returns false.
  //
  // Extra condition:
  // It turns out that under some circumstances, we can
  // fine repeats that were not "really" repeats (the matrices were not time
  // shifted) The situation was a bit obscure (it was a non-recurrent setup with
  // a lot of extra-right-context, where some inputs were never used), but to
  // prevent it happening again we are now checking in addition to the above,
  // that the time-shift between the segments (i.e. time_offsets[j] -
  // time_offsets[i]), has the "expected value" based on the assumption that
  // each segment should be shifted relative to the previous segment, by
  // 'time_shift_per_segment'.
  static bool FindFirstRepeat(
      const std::vector<std::vector<std::pair<int32, int32> > > &normalized_active_pairs,
      const std::vector<int32> &time_offsets,
      int32 time_shift_per_segment,
      int32 *seg1, int32 *seg2);

  // Converts a list of pairs (e.g. one of the elements of the output of
  // 'ConvertListsToPairLists)', back into a list of matrix indexes, using the
  // map 'pair_to_matrix'.
  static void PairListToMatrixList(
      const std::vector<std::pair<int32, int32> > &pair_list,
      const unordered_map<std::pair<int32, int32>, int32, PairHasher<int32> > &pair_to_matrix,
      std::vector<int32> *matrix_list);


  // This function just does some checking (via asserts), that
  // the lists of matrices 'list1' and 'list2' are of the same length,
  // that time_difference > 0, that each matrix with index m = list2[i] is of the
  // same dimension as the list1[i], with Cindexes that are the same except for
  // the time index being greater by 'time_difference'
  static void CheckIdentifiedMatrices(
      const NnetComputation &computation,
      const std::vector<int32> &list1,
      const std::vector<int32> &list2,
      int32 time_difference);


  // Given two command indexes command1 < command2 pointing to commands of type
  // kNoOperationMarker, this function modifies the computation by
  // removing all commands after command2, replacing command2 with a kGotoLabel
  // command pointing to command1  and then inserting just before command1
  // a marker of type kNoOperationLabel.
  static void FormInfiniteLoop(int32 command1, int32 command2,
                               NnetComputation *computation);

  // This is to be called after FormInfiniteLoop.  It inserts, just before
  // the final kGotoLabel command, commands that initialize
  // each of the matrices in list 'matrices1' from the corresponding
  // matrix in 'matrices2', using the kAllocMatrixFromOther command.
  // This effectively does, for example, matrices1[i] = matrices2[i],
  // while initializing matrices1[i] and deallocating matrices2[i];
  // it's implemented as a shallow swap.
  // It does this in such an order that even if the two lists are
  // not disjoint, the right thing happens.
  static void AddMatrixSwapCommands(
      const std::vector<int32> &matrices1,
      const std::vector<int32> &matrices2,
      NnetComputation *computation);


  // Called from AddMatrixSwapCommands, this function figures out for us
  // an acceptable order in which to execute the kAllocMatrixFromOther
  // commands.  This is easy to do if matrices1 and matrices2 are disjoint
  // sets, but has to be done more carefully if they overlap.
  // The output is a list of pairs where each pair (a, b) comes from
  // from matrices1 and matrices2 in the same position, i.e.
  // a = matrices1[i] and b = matrices2[i].
  static void GetMatrixSwapOrder(
      const std::vector<int32> &matrices1,
      const std::vector<int32> &matrices2,
      std::vector<std::pair<int32, int32> > *swaps);



  /// Given a list of command indexes ('segment_end_commands') which are
  /// expected to be command indexes of the kNoOperationMarker at segment
  /// boundaries, this function outputs for each of these command indexes a list
  /// of matrices which are 'active' at that point in time.  By 'active' we mean
  /// that the matrix has been written to before that time (note, we don't count
  /// initialization with zeros as being written to); and will be read after
  /// that time.  These is the list of matrices that 'need to be in scope'
  /// at those points in time.  '*active_matrices' is indexed by the
  /// same index as 'segment_end_commands', and is then a list of active
  /// matrices, in numerical order of matrix index.
  /// Note: for each i, (*active_matrices)[i] will be sorted and unique.
  static void FindActiveMatrices(const NnetComputation &computation,
                                 const Analyzer &analyzer,
                                 const std::vector<int32> &segment_end_commands,
                                 std::vector<std::vector<int32> > *active_matrices);


  const Nnet &nnet_;
  NnetComputation *computation_;
  Analyzer analyzer_;
  std::vector<std::pair<int32, int32> > matrix_to_pair_;

  std::vector<int32> segment_end_commands_;


};


// static
int32 ComputationLoopedOptimizer::FindTimeShift(
    const NnetComputation &computation,
    const std::vector<int32> &segment_ends) {
  KALDI_ASSERT(segment_ends.size() >= 3);
  // Ignore the first segment as it tends to be a special case
  // (it has more left context),
  int32 second_segment_begin = segment_ends[0],
      third_segment_begin = segment_ends[1],
      fourth_segment_begin = segment_ends[2];
  int32 first_output_command_seg2 = -1,
      first_output_command_seg3 = -1;
  for (int32 c = second_segment_begin; c < third_segment_begin; c++)
    if (computation.commands[c].command_type == kProvideOutput &&
        first_output_command_seg2 < 0)
      first_output_command_seg2 = c;
  for (int32 c = third_segment_begin; c < fourth_segment_begin; c++)
    if (computation.commands[c].command_type == kProvideOutput &&
        first_output_command_seg3 < 0)
      first_output_command_seg3 = c;
  if (first_output_command_seg2 < 0 ||
      first_output_command_seg3 < 0)
    KALDI_ERR << "Could not locate output commands for segments 2 and 3.";
  const NnetComputation::Command
      &command2 = computation.commands[first_output_command_seg2],
      &command3 = computation.commands[first_output_command_seg3];
  int32 seg2_node = command2.arg2, seg3_node = command3.arg2;
  KALDI_ASSERT(seg2_node == seg3_node);
  int32 seg2_submatrix = command2.arg1,
      seg3_submatrix = command3.arg1;
  KALDI_ASSERT(computation.IsWholeMatrix(seg2_submatrix) &&
               computation.IsWholeMatrix(seg3_submatrix));
  int32 seg2_matrix = computation.submatrices[seg2_submatrix].matrix_index,
      seg3_matrix = computation.submatrices[seg3_submatrix].matrix_index;
  KALDI_ASSERT(computation.matrices[seg2_matrix].num_rows ==
               computation.matrices[seg3_matrix].num_rows);
  KALDI_ASSERT(!computation.matrix_debug_info.empty());
  const NnetComputation::MatrixDebugInfo
      &debug_info2 = computation.matrix_debug_info[seg2_matrix],
      &debug_info3 = computation.matrix_debug_info[seg3_matrix];
  int32 t_offset = debug_info3.cindexes[0].second.t -
      debug_info2.cindexes[0].second.t;
  int32 num_rows = debug_info2.cindexes.size();
  for (int32 r = 0; r < num_rows; r++) {
    KALDI_ASSERT(debug_info3.cindexes[r].second.t ==
                 debug_info2.cindexes[r].second.t + t_offset);
  }
  return t_offset;
}

// static
void ComputationLoopedOptimizer::CreateMatrixPairs(
    const NnetComputation &computation,
    std::vector<std::pair<int32, int32> > *matrix_to_pair) {
  typedef unordered_map<std::vector<Cindex>, int32,
                        CindexVectorHasher> MapType;
  int32 cur_vector_id = 1;
  // Note: cindex_map just maps the vector<Cindex> to a unique value,
  // and then we manually work out a unique id that takes into
  // account the 'is_deriv' values.
  MapType cindex_map;
  int32 num_matrices = computation.matrices.size();
  matrix_to_pair->resize(num_matrices);
  KALDI_ASSERT(computation.matrix_debug_info.size() == num_matrices);
  for (int32 m = 1; m < num_matrices; m++) {
    KALDI_ASSERT(!computation.matrix_debug_info[m].cindexes.empty());
    std::vector<Cindex> cindexes = computation.matrix_debug_info[m].cindexes;
    int32 t_offset = cindexes[0].second.t;
    for (std::vector<Cindex>::iterator iter = cindexes.begin();
         iter != cindexes.end(); ++iter)
      iter->second.t -= t_offset;
    MapType::const_iterator iter = cindex_map.find(cindexes);
    int32 vector_id;
    if (iter != cindex_map.end()) {
      vector_id = iter->second;
    } else {
      vector_id = cur_vector_id++;
      cindex_map[cindexes] = vector_id;
    }
    bool is_deriv = computation.matrix_debug_info[m].is_deriv;
    int32 unique_id = 2 * vector_id + (is_deriv ? 1 : 0);
    (*matrix_to_pair)[m].first = unique_id;
    (*matrix_to_pair)[m].second = t_offset;
  }
}

// static
void ComputationLoopedOptimizer::GetPairToMatrixMap(
      std::vector<std::pair<int32, int32> > &matrix_to_pair,
      unordered_map<std::pair<int32, int32>, int32, PairHasher<int32> > *pair_to_matrix) {
  int32 num_matrices = matrix_to_pair.size();
  // actually there are one fewer matrices than num_matrices.
  pair_to_matrix->clear();
  for (int32 m = 1; m < num_matrices; m++)
    (*pair_to_matrix)[matrix_to_pair[m]] = m;
}


// static
void ComputationLoopedOptimizer::ConvertListsToPairLists(
      const std::vector<std::vector<int32> > &active_matrices,
      const std::vector<std::pair<int32, int32> > &matrix_to_pair,
      std::vector<std::vector<std::pair<int32, int32> > > *active_pairs) {
  active_pairs->clear();
  active_pairs->resize(active_matrices.size());
  int32 num_matrices = matrix_to_pair.size();
  for (size_t seg = 0; seg < active_matrices.size(); seg++) {
    const std::vector<int32> &this_active_matrix_list = active_matrices[seg];
    std::vector<std::pair<int32, int32> > &this_active_pair_list =
        (*active_pairs)[seg];
    this_active_pair_list.resize(this_active_matrix_list.size());
    std::vector<int32>::const_iterator iter = this_active_matrix_list.begin(),
        end = this_active_matrix_list.end();
    std::vector<std::pair<int32, int32> >::iterator
        out_iter = this_active_pair_list.begin();
    for (; iter != end; ++iter, ++out_iter) {
      KALDI_ASSERT(*iter > 0 && *iter < num_matrices);
      *out_iter = matrix_to_pair[*iter];
    }
  }
}

// static
void ComputationLoopedOptimizer::NormalizePairLists(
    std::vector<std::vector<std::pair<int32, int32> > > *active_pairs,
    std::vector<int32> *time_offsets) {
  int32 num_segments = active_pairs->size();
  time_offsets->resize(num_segments);
  for (int32 seg = 0; seg < num_segments; seg++) {
    std::vector<std::pair<int32, int32> > &this_pairs = (*active_pairs)[seg];
    std::sort(this_pairs.begin(), this_pairs.end());
    int32 this_offset;
    if (!this_pairs.empty()) {
      this_offset = this_pairs[0].second;
    } else {
      // if this_pairs is empty, produce arbitrary offsets that are increasing
      // (this will keep some self-testing code happy).
      if (seg == 0) { this_offset = 0; }
      else { this_offset = (*time_offsets)[seg - 1] + 1; }
    }
    (*time_offsets)[seg] = this_offset;
    std::vector<std::pair<int32, int32> >::iterator
        iter = this_pairs.begin(), end = this_pairs.end();
    for (; iter != end; ++iter)
      iter->second -= this_offset;
  }
}


// static
bool ComputationLoopedOptimizer::FindFirstRepeat(
    const std::vector<std::vector<std::pair<int32, int32> > > &normalized_active_pairs,
    const std::vector<int32> &time_offsets,
    int32 time_shift_per_segment,
    int32 *seg1, int32 *seg2) {
  int32 num_segments = normalized_active_pairs.size();
  // This algorithm may seem like it would be very slow, but the number of
  // segments will normally be quite small (e.g. 10), and the comparison of
  // elements of 'normalized_active_pairs' should be fast in cases where they
  // differ.
  KALDI_ASSERT(num_segments >= 2);

  bool perform_time_offset_check = true;
  if (normalized_active_pairs.back().empty()) {
    // If there are no variables active after the end of the last-but-one segment
    // (which is the last element in segment_ends, since we remove the end of the
    // very last segment), then don't perform the check related to
    // time-offsets, it's not relevant.  [this would probably be a computation
    // that doesn't require any context].
    perform_time_offset_check = false;
  }
  for (int32 s = 0; s < num_segments; s++) {
    for (int32 t = s + 1; t < num_segments; t++) {
      if ((!perform_time_offset_check ||
           time_offsets[t]-time_offsets[s] == (t-s) * time_shift_per_segment) &&
          normalized_active_pairs[s] == normalized_active_pairs[t]) {
        *seg1 = s;
        *seg2 = t;
        return true;
      }
    }
  }
  return false;
}

// static
void ComputationLoopedOptimizer::PairListToMatrixList(
    const std::vector<std::pair<int32, int32> > &pair_list,
    const unordered_map<std::pair<int32, int32>, int32, PairHasher<int32> > &pair_to_matrix,
    std::vector<int32> *matrix_list) {
  matrix_list->resize(pair_list.size());
  std::vector<std::pair<int32, int32> >::const_iterator
      iter = pair_list.begin(), end = pair_list.end();
  std::vector<int32>::iterator out_iter = matrix_list->begin();
  for (; iter != end; ++iter, ++out_iter) {
    unordered_map<std::pair<int32, int32>, int32,
                  PairHasher<int32> >::const_iterator
        map_iter = pair_to_matrix.find(*iter);
    if (map_iter == pair_to_matrix.end()) {
      KALDI_ERR << "Could not find pair in map (code error)";
    }
    *out_iter = map_iter->second;
  }
}



// static
void ComputationLoopedOptimizer::FindActiveMatrices(
    const NnetComputation &computation,
    const Analyzer &analyzer,
    const std::vector<int32> &segment_end_commands,
    std::vector<std::vector<int32> > *active_matrices) {
  int32 num_matrices = computation.matrices.size();
  int32 num_segments = segment_end_commands.size();
  active_matrices->clear();
  active_matrices->resize(num_segments);
  // this object just makes available some extra functions, vs. the Analyzer
  // object.
  ComputationAnalysis analysis(computation, analyzer);
  KALDI_ASSERT(IsSortedAndUniq(segment_end_commands));

  // the following vector gives us, for each matrix index, a submatrix index
  // that covers the whole of that matrix (needed by interface of 'analysis' object).
  std::vector<int32> whole_submatrices;
  computation.GetWholeSubmatrices(&whole_submatrices);
  for (int32 m = 1; m < num_matrices; m++) {
    // the following are command indexes, comparable with the indexes
    // in 'segment_end_commands'.
    int32 s = whole_submatrices[m],  // submatrix consisting of the whole of
                                     // 'm'.
        first_access = analysis.FirstAccess(s),
        last_access = analysis.LastAccess(s);
    for (int32 seg = 0; seg < num_segments; seg++) {
      int32 segment_end = segment_end_commands[seg];
      if (first_access < segment_end && last_access > segment_end) {
        // If the block of time during which the matrix is accessed, includes
        // this segment end-point, then the matrix is considered 'active' at
        // that time.
        (*active_matrices)[seg].push_back(m);
      }
    }
  }
}

// static
void ComputationLoopedOptimizer::CheckIdentifiedMatrices(
    const NnetComputation &computation,
    const std::vector<int32> &list1,
    const std::vector<int32> &list2,
    int32 time_difference) {
  KALDI_ASSERT(time_difference > 0);
  KALDI_ASSERT(list1.size() == list2.size());
  KALDI_ASSERT(!computation.matrix_debug_info.empty());
  for (size_t i = 0; i < list1.size(); i++) {
    int32 m1 = list1[i], m2 = list2[i];
    const NnetComputation::MatrixInfo
        &matrix_info1 = computation.matrices[m1],
        &matrix_info2 = computation.matrices[m2];
    KALDI_ASSERT(matrix_info1.num_rows == matrix_info2.num_rows &&
                 matrix_info1.num_cols == matrix_info2.num_cols &&
                 matrix_info1.stride_type == matrix_info2.stride_type);
    const NnetComputation::MatrixDebugInfo
        &debug_info1 = computation.matrix_debug_info[m1],
        &debug_info2 = computation.matrix_debug_info[m2];
    KALDI_ASSERT(debug_info1.is_deriv == debug_info2.is_deriv);
    KALDI_ASSERT(debug_info1.cindexes.size() == debug_info2.cindexes.size());
    std::vector<Cindex>::const_iterator iter1 = debug_info1.cindexes.begin(),
        end1 = debug_info1.cindexes.end(),
        iter2 = debug_info2.cindexes.begin();
    for (; iter1 != end1; iter1++,iter2++) {
      KALDI_ASSERT(iter2->first == iter1->first &&
                   iter2->second.n == iter1->second.n &&
                   iter2->second.t == iter1->second.t + time_difference &&
                   iter2->second.x == iter1->second.x);
    }
  }
}


// static
void ComputationLoopedOptimizer::GetMatrixSwapOrder(
    const std::vector<int32> &matrices1,
    const std::vector<int32> &matrices2,
    std::vector<std::pair<int32, int32> > *swaps) {
  KALDI_ASSERT(matrices1.size() == matrices2.size());
  swaps->clear();
  int32 num_matrices = matrices1.size();
  std::vector<bool> processed(num_matrices, false);
  std::vector<int32> queue;

  // num_loops is just for infinite-loop detection.
  int32 num_loops = 0;
  for (; static_cast<int32>(swaps->size()) < num_matrices; num_loops++) {
    for (int32 i = 0; i < num_matrices; i++) {
      if (processed[i])
        continue;
      int32 m1 = matrices1[i], m2 = matrices2[i];
      std::vector<int32>::const_iterator iter =
          std::lower_bound(matrices2.begin(), matrices2.end(), m1);
      if (iter == matrices2.end() || *iter != m1) {
        // Matrix m1 does not appear in the list 'matrices2', so
        // we are safe to process it at any time.
        swaps->push_back(std::pair<int32,int32>(m1, m2));
        processed[i] = true;
      } else {
        int32 m1_pos_in_matrices2 = iter - matrices2.begin();
        if (processed[m1_pos_in_matrices2]) {
          // We're safe to do this swap now, because the matrix m1 has already
          // appeared on the RHS of a swap, and by this point has been
          // deallocated, in effect.
          swaps->push_back(std::pair<int32,int32>(m1, m2));
          processed[i] = true;
        }
        // else do nothing, we cannot process m1 yet because
        // at this point in the computation it is still allocated.
      }
    }
    // The following assert is to check that we don't loop infinitely.  We can
    // prove that infinite looping won't happen, after on proving that there can
    // be no cycles like (m1, m2), (m2, m3), (m3, m1) (the length of 3 is chosen
    // arbitrarily as an example).  If such a cycle existed, we can reach a
    // contradiction based on the time-index (t) of the first cindex in m1.
    // Define t1 = that time index, t2 the same for m2, t3 the same for m3.  The
    // existence of the three pairs [as pairs like (matrices1[i], matrices2[i])]
    // implies that t2 > t1, t3 > t2, and t1 > t3 respectively, but this is
    // impossible.
    // This shows that all chains of dependencies must terminate.
    KALDI_ASSERT(num_loops <= num_matrices);
  }
}

// static
void ComputationLoopedOptimizer::AddMatrixSwapCommands(
    const std::vector<int32> &matrices1,
    const std::vector<int32> &matrices2,
    NnetComputation *computation) {
  std::vector<std::pair<int32, int32> > swaps;
  // Note: in 'easy' cases where matrices1 and matrices2 are disjoint,
  // 'swaps' will just be the vector { (matrices1[0],matrices2[0]),
  // (matrices1[1],matrices2[1]), ... },
  // but in some cases these may need to get reordered.
  GetMatrixSwapOrder(matrices1, matrices2, &swaps);

  NnetComputation::Command goto_label_command = computation->commands.back();
  KALDI_ASSERT(goto_label_command.command_type == kGotoLabel);
  computation->commands.pop_back();

  // the following vector gives us, for each matrix index, a submatrix index
  // that covers the whole of that matrix (needed because the commands
  // require submatrix indexes)
  std::vector<int32> whole_submatrices;
  computation->GetWholeSubmatrices(&whole_submatrices);
  size_t num_matrices = whole_submatrices.size();

  for (size_t i = 0; i < swaps.size(); i++) {
    int32 m1 = swaps[i].first, m2 = swaps[i].second;
    KALDI_ASSERT(static_cast<size_t>(m1) < num_matrices &&
                 static_cast<size_t>(m2) < num_matrices);
    int32 s1 = whole_submatrices[m1], s2 = whole_submatrices[m2];
    computation->commands.push_back(
        NnetComputation::Command(
            kAllocMatrixFromOther, s1, s2));
  }
  computation->commands.push_back(goto_label_command);
}

// static
void ComputationLoopedOptimizer::FormInfiniteLoop(
    int32 command1, int32 command2,
    NnetComputation *computation) {
  KALDI_ASSERT(static_cast<int32>(computation->commands.size()) >=
               command2 + 1 && command1 < command2);
  KALDI_ASSERT(
      computation->commands[command1].command_type == kNoOperationMarker &&
      computation->commands[command2].command_type == kNoOperationMarker);
  // Remove any commands after 'command2'.
  computation->commands.resize(command2 + 1);
  computation->commands[command2].command_type = kGotoLabel;
  computation->commands[command2].arg1 = command1;
  NnetComputation::Command c(kNoOperationLabel);
  computation->commands.insert(computation->commands.begin() + command1,
                               c);
  // Now the kNoOperationLabel command is at position 'command1'.
}



bool ComputationLoopedOptimizer::Optimize() {
  analyzer_.Init(nnet_, *computation_);
  KALDI_ASSERT(!computation_->matrix_debug_info.empty() &&
               "You must request matrix debug info when compiling "
               "looped computations.");

  // get the indexes of the separator commands at the ends of segments.
  std::vector<int32> segment_ends;
  GetSegmentEnds(*computation_, &segment_ends);
  int32 time_shift_per_segment = FindTimeShift(*computation_,
                                               segment_ends);

  // Ignore the end of the very last segment- it is not a candidate for a
  // 'splice point'.  What we're doing here is like creating a tape loop; we
  // have to find a place where the list of variables is the same except for a
  // time offset.
  // [note: it's not exactly like a tape loop because the prologue can
  // vary... the sequence is of the form like a b b b b b .. ]
  segment_ends.pop_back();


  std::vector<std::vector<int32> > active_matrices;
  // Find the list of matrices active at each of those segment-end-command
  // times.
  FindActiveMatrices(*computation_, analyzer_, segment_ends,
                     &active_matrices);

  // Find a representation of the matrices of the computation as pairs
  // (unique_id, time_offset) that are more amenable to finding
  // matrices that represet lists of Cindexes that differ only by
  // a time offset.
  std::vector<std::pair<int32, int32> > matrix_to_pair;
  CreateMatrixPairs(*computation_, &matrix_to_pair);

  // Create the reverse map from pair to matrix index; we'll need it.
  unordered_map<std::pair<int32, int32>, int32, PairHasher<int32> > pair_to_matrix;
  GetPairToMatrixMap(matrix_to_pair, &pair_to_matrix);

  // get lists of matrix per segment in the pair representation.
  std::vector<std::vector<std::pair<int32, int32> > > pair_lists;
  ConvertListsToPairLists(active_matrices, matrix_to_pair,
                          &pair_lists);

  std::vector<int32> time_offsets;
  NormalizePairLists(&pair_lists, &time_offsets);

  // Note: seg1 and seg2 are indexes into 'segment_ends', representing
  // points in time (that happen to be the ends of segments).
  int32 seg1, seg2;
  if (!FindFirstRepeat(pair_lists,
                       time_offsets,
                       time_shift_per_segment,
                       &seg1, &seg2)) {
    KALDI_VLOG(2) << "Could not find repeats of variables.";
    return false;
  }

  // reverse the normalization for segments seg1 and seg2.
  for (size_t i = 0; i < pair_lists[seg1].size(); i++)
    pair_lists[seg1][i].second += time_offsets[seg1];
  for (size_t i = 0; i < pair_lists[seg2].size(); i++)
    pair_lists[seg2][i].second += time_offsets[seg2];
  std::vector<int32> seg1_matrices, seg2_matrices;
  PairListToMatrixList(pair_lists[seg1], pair_to_matrix, &seg1_matrices);
  PairListToMatrixList(pair_lists[seg2], pair_to_matrix, &seg2_matrices);

  int32 time_difference = time_offsets[seg2] - time_offsets[seg1];
  CheckIdentifiedMatrices(*computation_, seg1_matrices, seg2_matrices,
                          time_difference);


  FormInfiniteLoop(segment_ends[seg1], segment_ends[seg2], computation_);

  AddMatrixSwapCommands(seg1_matrices, seg2_matrices, computation_);

  RenumberComputation(computation_);

  FixGotoLabel(computation_);

  return true;
}


void OptimizeLoopedComputation(const Nnet &nnet,
                               NnetComputation *computation) {
  ComputationLoopedOptimizer optimizer(nnet, computation);
  optimizer.Optimize();
}


void FixGotoLabel(NnetComputation *computation) {
  int32 num_commands = computation->commands.size();
  if (num_commands == 0)
    return;
  if (computation->commands[num_commands-1].command_type == kGotoLabel) {
    int32 dest_command = computation->commands[num_commands-1].arg1;
    if (static_cast<size_t>(dest_command) <  computation->commands.size() &&
        computation->commands[dest_command].command_type == kNoOperationLabel)
      return;  // nothing to fix.
    for (int32 c = 0; c + 1 < num_commands; c++) {
      if (computation->commands[c].command_type == kNoOperationLabel) {
        computation->commands[num_commands-1].arg1 = c;
        return;
      }
    }
    KALDI_ERR << "Label not found.";
  }
}



} // namespace nnet3
} // namespace kaldi

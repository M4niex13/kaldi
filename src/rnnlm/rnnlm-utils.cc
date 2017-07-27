// rnnlm/rnnlm-utils.cc

// Copyright 2017  Daniel Povey
//                 Hossein Hadian

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

#include <numeric>
#include "rnnlm/rnnlm-utils.h"


namespace kaldi {
namespace rnnlm {


void ReadSparseWordFeatures(std::istream &is,
                            int32 feature_dim,
                            SparseMatrix<BaseFloat> *word_feature_matrix) {
  std::vector<std::vector<std::pair<MatrixIndexT, BaseFloat> > > sparse_rows;
  std::string line;
  int32 line_number = 0;
  while (std::getline(is, line)) {
    std::vector<std::pair<MatrixIndexT, BaseFloat> > row;
    std::istringstream line_is(line);
    int32 word_id;
    line_is >> word_id;
    line_is >> std::ws;
    KALDI_ASSERT(word_id == line_number++);

    int32 feature_index;
    BaseFloat feature_value;
    while (line_is >> feature_index)
    {
      KALDI_ASSERT(feature_index >= 0 && feature_index < feature_dim);
      line_is >> std::ws;
      if (!(line_is >> feature_value))
        KALDI_ERR << "No value for feature-index " << feature_index;
      row.push_back(std::make_pair(feature_index, feature_value));
      if (row.size() > 1)  // check the indexes are in increasing order
        KALDI_ASSERT(row.back().first > row.rbegin()[1].first);
    }
    sparse_rows.push_back(row);
  }
  if (sparse_rows.size() < 1)
    KALDI_ERR << "No line could be read from the file.";
  word_feature_matrix->CopyFromSmat(
      SparseMatrix<BaseFloat>(feature_dim, sparse_rows));
}


}  // namespace rnnlm
}  // namespace kaldi

// nnet3/nnet-computation.cc

// nnet3/nnet-computation.cc

// Copyright      2015  Johns Hopkins University (author: Daniel Povey)
//                2015  Xiaohui Zhang

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

#include <iterator>
#include <sstream>
#include "nnet3/nnet-computation.h"

namespace kaldi {
namespace nnet3 {

bool ComputationRequest::NeedDerivatives() const {
  bool ans = false;
  if (need_model_derivative)
    ans = true;
  for (size_t i = 0; i < inputs.size(); i++) {
    if (inputs[i].has_deriv) { // derivative requested for this input
      ans = true;
      break;
    }
  }
  if (ans) {
    // check that the output actually provides a derivative, else the
    // request could not be meaningfully satisfied.
    size_t i;
    for (i = 0; i < outputs.size(); i++)
      if (outputs[i].has_deriv)
        break;
    if (i == outputs.size()) {
      KALDI_ERR << "You requested model derivatives or input derivatives, but "
                << "provide no derivatives at the output.";
    }
  }
  return ans;
}

int32 ComputationRequest::IndexForInput(
    const std::string &node_name) const {
  int32 ans = -1;
  for (size_t i = 0; i < inputs.size(); i++) {
    if (inputs[i].name == node_name) {
      KALDI_ASSERT(ans == -1 && "Two inputs with the same name");
      ans = i;
    }
  }
  return ans;
}

int32 ComputationRequest::IndexForOutput(
    const std::string &node_name) const {
  int32 ans = -1;
  for (size_t i = 0; i < outputs.size(); i++) {
    if (outputs[i].name == node_name) {
      KALDI_ASSERT(ans == -1 && "Two inputs with the same name");
      ans = i;
    }
  }
  return ans;
}

NnetComputation::~NnetComputation() {
  for (size_t i = 0; i < component_precomputed_indexes.size(); i++)
    delete component_precomputed_indexes[i];
}

void NnetComputation::ComputeCudaIndexes() {
  indexes_cuda.resize(indexes.size());

  for (size_t i = 0; i < indexes.size(); i++)
    indexes_cuda[i].CopyFromVec(indexes[i]);

  KALDI_ASSERT(sizeof(Int32Pair) == sizeof(std::pair<int32,int32>));
  indexes_ranges_cuda.resize(indexes_ranges.size());
  for (int32 i = 0; i < indexes_ranges.size(); i++) {
    const std::vector<std::pair<int32,int32> > *input = &(indexes_ranges[i]);
    const std::vector<Int32Pair> *input_cast =
        reinterpret_cast<const std::vector<Int32Pair> *>(input);
    // note: the indexes for CUDA use can't very easily use STL types due to
    // the interface of CUDA being plain C.
    indexes_ranges_cuda[i].CopyFromVec(*input_cast);
  }
}

int32 NnetComputation::NewSubMatrix(int32 base_submatrix,
                                    int32 row_offset, int32 num_rows,
                                    int32 col_offset, int32 num_cols) {
  KALDI_ASSERT(base_submatrix > 0 &&
               static_cast<size_t>(base_submatrix) < submatrices.size());
  const SubMatrixInfo &base_info = submatrices[base_submatrix];
  int32 base_matrix = base_info.matrix_index;
  KALDI_ASSERT(base_matrix > 0 &&
               static_cast<size_t>(base_matrix) < matrices.size());
  if (num_rows == -1) // we interpret this to mean 'as many as possible'.
    num_rows = base_info.num_rows - row_offset;
  if (num_cols == -1) // we interpret this to mean 'as many as possible'.
    num_cols = base_info.num_cols - col_offset;
  KALDI_ASSERT(row_offset + num_rows <= base_info.num_rows &&
               col_offset + num_cols <= base_info.num_cols &&
               row_offset >= 0 && col_offset >= 0 &&
               num_rows > 0 && num_cols > 0);
  int32 matrix_row_offset = base_info.row_offset + row_offset,
      matrix_col_offset = base_info.col_offset + col_offset;
  int32 ans = submatrices.size();
  submatrices.push_back(
      NnetComputation::SubMatrixInfo(base_matrix, matrix_row_offset, num_rows,
                                     matrix_col_offset, num_cols));
  return ans;
}

int32 NnetComputation::NewMatrix(int32 num_rows, int32 num_cols) {
  KALDI_ASSERT(num_rows > 0 && num_cols > 0);
  if (matrices.empty()) {  // Set up the zero matrix; index zero is reserved.
    matrices.push_back(MatrixInfo(0, 0));
    submatrices.push_back(SubMatrixInfo(0, 0, 0, 0, 0));
  }
  int32 matrix_index = matrices.size(),
      submatrix_index = submatrices.size();
  matrices.push_back(MatrixInfo(num_rows, num_cols));
  if (!matrix_debug_info.empty())
    matrix_debug_info.push_back(MatrixDebugInfo());
  submatrices.push_back(SubMatrixInfo(matrix_index, 0, num_rows, 0, num_cols));
  return submatrix_index;
}

void NnetComputation::MatrixInfo::Read(std::istream &is, bool binary) {
  ExpectToken(is, binary, "<MatrixInfo>");
  ExpectToken(is, binary, "<NumRows>");
  ReadBasicType(is, binary, &num_rows);
  ExpectToken(is, binary, "<NumCols>");
  ReadBasicType(is, binary, &num_cols);
  ExpectToken(is, binary, "</MatrixInfo>");
}

void NnetComputation::MatrixInfo::Write(std::ostream &os, bool binary) const {
  WriteToken(os, binary, "<MatrixInfo>");
  WriteToken(os, binary, "<NumRows>");
  WriteBasicType(os, binary, num_rows);
  WriteToken(os, binary, "<NumCols>");
  WriteBasicType(os, binary, num_cols);
  WriteToken(os, binary, "</MatrixInfo>");
}

void NnetComputation::MatrixDebugInfo::Swap(
    NnetComputation::MatrixDebugInfo *other) {
  std::swap(is_deriv, other->is_deriv);
  cindexes.swap(other->cindexes);
}

void NnetComputation::MatrixDebugInfo::Read(std::istream &is, bool binary) {
  ExpectToken(is, binary, "<MatrixDebugInfo>");
  ExpectToken(is, binary, "<IsDeriv>");
  ReadBasicType(is, binary, &is_deriv);
  ExpectToken(is, binary, "<Cindexes>");
  ReadCindexVector(is, binary, &cindexes);
  ExpectToken(is, binary, "</MatrixDebugInfo>");
}

void NnetComputation::MatrixDebugInfo::Write(std::ostream &os, bool binary) const {
  WriteToken(os, binary, "<MatrixDebugInfo>");
  WriteToken(os, binary, "<IsDeriv>");
  WriteBasicType(os, binary, is_deriv);
  WriteToken(os, binary, "<Cindexes>");
  WriteCindexVector(os, binary, cindexes);
  WriteToken(os, binary, "</MatrixDebugInfo>");
}

void NnetComputation::SubMatrixInfo::Read(std::istream &is, bool binary) {
  ExpectToken(is, binary, "<SubMatrixInfo>");
  ExpectToken(is, binary, "<MatrixIndex>");
  ReadBasicType(is, binary, &matrix_index);
  ExpectToken(is, binary, "<RowOffset>");
  ReadBasicType(is, binary, &row_offset);
  ExpectToken(is, binary, "<NumRows>");
  ReadBasicType(is, binary, &num_rows);
  ExpectToken(is, binary, "<ColOffset>");
  ReadBasicType(is, binary, &col_offset);
  ExpectToken(is, binary, "<NumCols>");
  ReadBasicType(is, binary, &num_cols);
  ExpectToken(is, binary, "</SubMatrixInfo>");
}

void NnetComputation::SubMatrixInfo::Write(std::ostream &os, bool binary) const {
  WriteToken(os, binary, "<SubMatrixInfo>");
  WriteToken(os, binary, "<MatrixIndex>");
  WriteBasicType(os, binary, matrix_index);
  WriteToken(os, binary, "<RowOffset>");
  WriteBasicType(os, binary, row_offset);
  WriteToken(os, binary, "<NumRows>");
  WriteBasicType(os, binary, num_rows);
  WriteToken(os, binary, "<ColOffset>");
  WriteBasicType(os, binary, col_offset);
  WriteToken(os, binary, "<NumCols>");
  WriteBasicType(os, binary, num_cols);
  WriteToken(os, binary, "</SubMatrixInfo>");
}

void NnetComputation::Command::Read(std::istream &is, bool binary) {
  ExpectToken(is, binary, "<Command>");
  if (binary) {
    int32 command_type_int;
    ReadBasicType(is, binary, &command_type_int);
    command_type = static_cast<CommandType>(command_type_int);
  } else {
    std::string command_type_str;
    getline(is, command_type_str); 
    if (command_type_str == "kAllocMatrixZeroed") {
      command_type = kAllocMatrixZeroed;
    } else if (command_type_str == "kAllocMatrixUndefined") {
      command_type = kAllocMatrixUndefined;
    } else if (command_type_str == "kDeallocMatrix") {
      command_type = kDeallocMatrix;
    } else if (command_type_str == "kAllocMatrixFromOther") {
      command_type = kAllocMatrixFromOther;
    } else if (command_type_str == "kAllocMatrixFromOtherZeroed") {
      command_type = kAllocMatrixFromOtherZeroed;
    } else if (command_type_str == "kPropagate") {
      command_type = kPropagate;
    } else if (command_type_str == "kStoreStats") {
      command_type = kStoreStats;
    } else if (command_type_str == "kBackprop") {
      command_type = kBackprop;
    } else if (command_type_str == "kBackpropNoModelUpdate") {
      command_type = kBackpropNoModelUpdate;
    } else if (command_type_str == "kMatrixCopy") {
      command_type = kMatrixCopy;
    } else if (command_type_str == "kMatrixAdd") {
      command_type = kMatrixAdd;
    } else if (command_type_str == "kCopyRows") {
      command_type = kCopyRows;
    } else if (command_type_str == "kAddRows") {
      command_type = kAddRows;
    } else if (command_type_str == "kCopyRowsMulti") {
      command_type = kCopyRowsMulti;
    } else if (command_type_str == "kCopyToRowsMulti") {
      command_type = kCopyToRowsMulti;
    } else if (command_type_str == "kAddRowsMulti") {
      command_type = kAddRowsMulti;
    } else if (command_type_str == "kAddToRowsMulti") {
      command_type = kAddToRowsMulti;
    } else if (command_type_str == "kAddRowRanges") {
      command_type = kAddRowRanges;
    } else if (command_type_str == "kNoOperation") {
      command_type = kNoOperation;
    } else if (command_type_str == "kNoOperationMarker") {
      command_type = kNoOperationMarker;
    } else {
      KALDI_ERR << "Un-handled command type.";
    }
  }
  ExpectToken(is, binary, "</Command>");
}

void NnetComputation::Command::Write(std::ostream &os, bool binary) const {
  WriteToken(os, binary, "<Command>");
  if (binary) {
    WriteBasicType(os, binary, static_cast<int32>(command_type));
  } else {
    std::string command_type_str;
    switch (command_type) {
      case kAllocMatrixZeroed:
        os << "kAllocMatrixZeroed\n";
        break;
      case kAllocMatrixUndefined:
        os << "kAllocMatrixUndefined\n";
        break;
      case kDeallocMatrix:
        os << "kDeallocMatrix\n";
        break;
      case kAllocMatrixFromOther:
        os << "kAllocMatrixFromOther\n";
        break;
      case kAllocMatrixFromOtherZeroed:
        os << "kAllocMatrixFromOtherZeroed\n";
        break;
      case kPropagate:
        os << "kPropagate\n";
        break;
      case kStoreStats:
        os << "kStoreStats\n";
        break;
      case kBackprop:
        os << "kBackprop\n";
        break;
      case kBackpropNoModelUpdate:
        os << "kBackpropNoModelUpdate\n";
        break;
      case kMatrixCopy:
        os << "kMatrixCopy\n";
        break;
      case kMatrixAdd:
        os << "kMatrixAdd\n";
        break;
      case kCopyRows:
        os << "kCopyRows\n";
        break;
      case kAddRows:
        os << "kAddRows\n";
        break;
      case kCopyRowsMulti:
        os << "kCopyRowsMulti\n";
        break;
      case kCopyToRowsMulti:
        os << "kCopyToRowsMulti\n";
        break;
      case kAddRowsMulti:
        os << "kAddRowsMulti\n";
        break;
      case kAddToRowsMulti:
        os << "kAddToRowsMulti\n";
        break;
      case kAddRowRanges:
        os << "kAddRowRanges\n";
        break;
      case kNoOperation:
        os << "kNoOperation\n";
        break;
      case kNoOperationMarker:
        os << "kNoOperationMarker\n";
        break;
      default:
        KALDI_ERR << "Un-handled command type.";
    }
  }
  WriteToken(os, binary, "</Command>");
}



// outputs a string explaining the meaning each sub-matrix in vaguely
// matlab-like notation: for whole matrices, something like "m1", "m2";
// and for parts of matrices, "m1(0:10, 20:40)".
void NnetComputation::GetSubmatrixStrings(
    const Nnet &nnet, std::vector<std::string> *submat_strings) const {
  int32 num_submatrices = this->submatrices.size();
  KALDI_ASSERT(num_submatrices > 0);
  submat_strings->resize(num_submatrices);
  (*submat_strings)[0] = "[]";  // the empty matrix
  for (int32 i = 1; i < num_submatrices; i++) {
    const NnetComputation::SubMatrixInfo &submat = this->submatrices[i];
    std::ostringstream os;
    if (this->IsWholeMatrix(i)) {
      os << 'm' << submat.matrix_index;
    } else { // part of a range.
      os << 'm' << submat.matrix_index << '(' << submat.row_offset << ':'
         << (submat.row_offset + submat.num_rows - 1) << ", "
         << submat.col_offset << ':' << (submat.col_offset + submat.num_cols - 1)
         << ')';
    }
    (*submat_strings)[i] = os.str();
  }
}

// outputs a string containing a text form of each of the elements of the
// "indexes" vector: if indexes[i] is (1, 2, 3), then (*indexes_strings)[i]
// is "1,2,3".
static void GetIndexesStrings(const Nnet &nnet,
                              const NnetComputation &computation,
                              std::vector<std::string> *indexes_strings) {
  int32 size = computation.indexes.size();
  indexes_strings->resize(size);
  for (int32 i = 0; i < size; i++) {
    std::ostringstream os;
    PrintIntegerVector(os, computation.indexes[i]);
    (*indexes_strings)[i] = os.str();
  }
}

// outputs a string containing a text form of each of the elements of the
// "indexes_multi" vector.
static void GetIndexesMultiStrings(
    const Nnet &nnet,
    const NnetComputation &computation,
    std::vector<std::string> *indexes_multi_strings) {
  int32 indexes_multi_size = computation.indexes_multi.size();
  indexes_multi_strings->resize(indexes_multi_size);

  for (int32 i = 0; i < indexes_multi_size; i++) {
    std::ostringstream os;
    os << "[";
    const std::vector<std::pair<int32, int32> > &vec =
        computation.indexes_multi[i];
    int32 size = vec.size();
    for (int32 j = 0; j < size; j++) {
      int32 submat_index = vec[j].first, row_index = vec[j].second;
      if (submat_index == -1) {
        os << "NULL";
      } else {
        const NnetComputation::SubMatrixInfo &submat =
            computation.submatrices[submat_index];
        const NnetComputation::MatrixInfo &mat =
            computation.matrices[submat.matrix_index];
        int32 row = row_index + submat.row_offset;
        int32 col_start = submat.col_offset,
            col_end = col_start + submat.num_cols;
        if (!(row_index < submat.num_rows &&
              row < mat.num_rows)) {
          KALDI_WARN << "Invalid indexes in indexes-multi[" << i
                     << ": submatrix " << submat_index << " = m"
                     << submat.matrix_index << "(" << submat.row_offset
                     << ':' << (submat.row_offset + submat.num_rows - 1)
                     << ',' << submat.col_offset << ':'
                     << (submat.col_offset + submat.num_cols - 1) << ") has "
                     << submat.num_rows << " rows, but you access row "
                     << row_index;
        }
        if (col_start == 0 && col_end == mat.num_cols)
          os << 'm' << submat.matrix_index << '(' << row << ",:)";
        else
          os << 'm' << submat.matrix_index << '(' << row << ',' << col_start
             << ':' << (col_end - 1) << ')';
      }
      if (j + 1 < size) os << ",";
    }
    os << "]";
    (*indexes_multi_strings)[i] = os.str();
  }
}


// writes to "os" the statement for this command.
static void PrintCommand(std::ostream &os,
                         const Nnet &nnet,
                         const NnetComputation &computation,
                         int32 command_index,
                         const std::vector<std::string> &submatrix_strings,
                         const std::vector<std::string> &indexes_strings,
                         const std::vector<std::string> &indexes_multi_strings) {
  KALDI_ASSERT(command_index < computation.commands.size());
  os << "c" << command_index << ": ";
  const NnetComputation::Command &c = computation.commands[command_index];
  switch (c.command_type) {
    case kAllocMatrixZeroed:
      os << "m" << c.arg1 << " = zeros("
         << computation.matrices[c.arg1].num_rows
         << ',' << computation.matrices[c.arg1].num_cols << ")\n";
      break;
    case kAllocMatrixUndefined:
      os << "m" << c.arg1 << " = undefined("
         << computation.matrices[c.arg1].num_rows
         << ',' << computation.matrices[c.arg1].num_cols << ")\n";
      break;
    case kDeallocMatrix:
      os << "m" << c.arg1 << " = []\n";
      break;
    case kAllocMatrixFromOther:
      os << "m" << c.arg1 << ".swap(m" << c.arg2 << ") [dim = "
         << computation.matrices[c.arg1].num_rows << " x "
         << computation.matrices[c.arg1].num_cols << "]\n";
      break;
    case kAllocMatrixFromOtherZeroed:
      os << "m" << c.arg1 << ".swap(m" << c.arg2 << ") [dim = "
         << computation.matrices[c.arg1].num_rows << " x "
         << computation.matrices[c.arg1].num_cols << "]; m"
         << c.arg1 << ".zero();\n";
      break;
    case kPropagate:
      os << nnet.GetComponentName(c.arg1) << ".Propagate(";
      if (c.arg2 == 0) os << "NULL, ";
      else os << "precomputed_indexes[" << c.arg2 << "], ";
      os << submatrix_strings[c.arg3] << ", &" << submatrix_strings[c.arg4]
         << ")\n";
      break;
    case kStoreStats:
      os << nnet.GetComponentName(c.arg1) << ".StoreStats("
         << submatrix_strings[c.arg2] << ")\n";
      break;
    case kBackprop:
    case kBackpropNoModelUpdate: {
      int32 component_index = c.arg1;
      os << nnet.GetComponentName(component_index) << ".Backprop(";
      if (c.arg2 == 0) os << "NULL, ";
      else os << "precomputed_indexes[" << c.arg2 << "], ";
      os << submatrix_strings[c.arg3] << ", "
         << submatrix_strings[c.arg4] << ", "
         << submatrix_strings[c.arg5] << ", "
         << (computation.need_model_derivative &&
             c.command_type == kBackprop ?
             "[component-pointer], " : "NULL, ")
         << (c.arg6 == 0 ? std::string("NULL") :
             std::string("&") + submatrix_strings[c.arg6]) << ")\n";
      break;
    }
    case kMatrixCopy:
      os << submatrix_strings[c.arg1] << " = "
         << submatrix_strings[c.arg2] << "\n";
      break;
    case kMatrixAdd:
      os << submatrix_strings[c.arg1] << " += "
         << submatrix_strings[c.arg2] << "\n";
      break;
    case kAddRows:
    case kCopyRows:
      os << submatrix_strings[c.arg1] << "."
         << (c.command_type == kAddRows ? "AddRows" :
             "CopyRows") << "(" << submatrix_strings[c.arg2]
         << indexes_strings[c.arg3] << ")\n";
      break;
    case kAddRowsMulti:
    case kAddToRowsMulti:
    case kCopyRowsMulti:
    case kCopyToRowsMulti: {
      CommandType ct = c.command_type;
      os << submatrix_strings[c.arg1] << "."
         << (ct == kAddRowsMulti ? "AddRowsMulti" :
             (ct == kAddToRowsMulti? "AddToRowsMulti" :
              (ct == kCopyRowsMulti ? "CopyRowsMulti" :
               "CopyToRowsMulti"))) << "("
         << indexes_multi_strings[c.arg2] << ")\n";
      break;
    }
    case kAddRowRanges: {
      os << submatrix_strings[c.arg1] << ".AddRowRanges("
         << submatrix_strings[c.arg2] << ", [";
      const std::vector<std::pair<int32, int32> > &pairs =
           computation.indexes_ranges[c.arg3];
      for (size_t i = 0; i < pairs.size(); i++) {
        os << pairs[i].first << ":" << (pairs[i].second - 1);
        if (i + 1 < pairs.size()) os << ",";
      }
      os << "])\n";
      break;
    }
    case kNoOperation:
      os << "[no-op]\n";
      break;
    case kNoOperationMarker:
      os << "# begin backward commands\n";
      break;
    default:
      KALDI_ERR << "Un-handled command type.";
  }
}


static void PrintComputationPreamble(
    std::ostream &os,
    const NnetComputation &c,
    const Nnet &nnet,
    const std::vector<std::string> &submatrix_strings,
    const std::vector<std::string> &indexes_strings,
    const std::vector<std::string> &indexes_multi_strings) {

  // First print info about the matrices.
  os << "matrix ";
  for (int32 i = 1; i < c.matrices.size(); i++) {
    os << "m" << i << "(" << c.matrices[i].num_rows
       << ", " << c.matrices[i].num_cols << ")";
    if (i + 1 < c.matrices.size())
      os << ", ";
  }
  os << "\n";
  // show which matrices the inputs and outputs map to.
  for (unordered_map<int32, std::pair<int32, int32> >::const_iterator iter =
           c.input_output_info.begin(); iter != c.input_output_info.end();
       ++iter) {
    int32 node_index = iter->first,
        value_matrix_index = iter->second.first,
        deriv_matrix_index = iter->second.second;
    os << nnet.GetNodeName(node_index) << ".value -> m"
       << value_matrix_index << "\n";
    if (deriv_matrix_index != 0) {
      os << nnet.GetNodeName(node_index) << ".deriv -> m"
         << deriv_matrix_index << "\n";
    }
  }
  if (!c.matrix_debug_info.empty()) {
    os << "# The following show how matrices correspond to network-nodes and\n"
       << "# cindex-ids.  Format is: matrix = <node-id>.[value|deriv][ <list-of-cindex-ids> ]\n"
       << "# where a cindex-id is written as (n,t[,x]) but ranges of t values are compressed\n"
       << "# so we write (n, tfirst:tlast).\n";
    KALDI_ASSERT(c.matrix_debug_info.size() == c.matrices.size());
    for (int32 i = 1; i < c.matrices.size(); i++) {
      const NnetComputation::MatrixDebugInfo &debug_info =
          c.matrix_debug_info[i];
      os << "m" << i << " == " << (debug_info.is_deriv ? "deriv: " : "value: ");
      PrintCindexes(os, debug_info.cindexes, nnet.GetNodeNames());
      os << "\n";
    }
  }
}

void NnetComputation::Print(std::ostream &os, const Nnet &nnet) const {
  std::vector<std::string> submatrix_strings, indexes_strings,
      indexes_multi_strings;
  this->GetSubmatrixStrings(nnet, &submatrix_strings);
  GetIndexesStrings(nnet, *this, &indexes_strings);
  GetIndexesMultiStrings(nnet, *this, &indexes_multi_strings);
  PrintComputationPreamble(os, *this, nnet, submatrix_strings,
                           indexes_strings, indexes_multi_strings);
  os << "# begin forward commands\n";
  for (int32 c = 0; c < commands.size(); c++) {
    PrintCommand(os, nnet, *this, c, submatrix_strings,
                 indexes_strings, indexes_multi_strings);
  }
}

void NnetComputation::Read(std::istream &is, bool binary) {
  ExpectToken(is, binary, "<NnetComputation>");
  size_t num_matrices;
  ExpectToken(is, binary, "<NumMatrices>");
  ReadBasicType(is, binary, &num_matrices);
  KALDI_ASSERT(num_matrices >= 0);
  matrices.resize(num_matrices);
  ExpectToken(is, binary, "<Matrices>");
  for (size_t c = 0; c < num_matrices; c++) {
    matrices[c].Read(is, binary);
  }

  size_t num_matrix_debug_info;
  ExpectToken(is, binary, "<NumMatrixDebugInfo>");
  ReadBasicType(is, binary, &num_matrix_debug_info);
  KALDI_ASSERT(num_matrix_debug_info >= 0);
  matrix_debug_info.resize(num_matrix_debug_info);
  ExpectToken(is, binary, "<MatrixDebugInfo>");
  for (size_t c = 0; c < num_matrix_debug_info; c++) {
    matrix_debug_info[c].Read(is, binary);
  }

  size_t num_submatrices;
  ExpectToken(is, binary, "<NumSubMatrices>");
  ReadBasicType(is, binary, &num_submatrices);
  KALDI_ASSERT(num_submatrices >= 0);
  submatrices.resize(num_submatrices);
  ExpectToken(is, binary, "<SubMatrices>");
  for (size_t c = 0; c < num_submatrices; c++) {
    submatrices[c].Read(is, binary);
  }

  size_t num_indexes;
  ExpectToken(is, binary, "<NumIndexes>");
  ReadBasicType(is, binary, &num_indexes);
  KALDI_ASSERT(num_indexes >= 0);
  indexes.resize(num_indexes);
  ExpectToken(is, binary, "<Indexes>");
  for (size_t c = 0; c < num_indexes; c++) {
    ReadIntegerVector(is, binary, &(indexes[c]));
  }

  size_t num_indexes_multi;
  ExpectToken(is, binary, "<NumIndexesMulti>");
  ReadBasicType(is, binary, &num_indexes_multi);
  KALDI_ASSERT(num_indexes_multi >= 0);
  indexes_multi.resize(num_indexes_multi);
  ExpectToken(is, binary, "<IndexesMulti>");
  for (size_t c = 0; c < num_indexes_multi; c++) {
    ReadIntegerPairVector(is, binary, &(indexes_multi[c]));
  }

  size_t num_indexes_ranges;
  ExpectToken(is, binary, "<NumIndexesRanges>");
  ReadBasicType(is, binary, &num_indexes_ranges);
  KALDI_ASSERT(num_indexes_ranges >= 0);
  indexes_ranges.resize(num_indexes_ranges);
  ExpectToken(is, binary, "<IndexesRanges>");
  for (size_t c = 0; c < num_indexes_ranges; c++) {
    ReadIntegerPairVector(is, binary, &(indexes_ranges[c]));
  }

  size_t num_input_output_info;
  ExpectToken(is, binary, "<NumInputOutputInfo>");
  ReadBasicType(is, binary, &num_input_output_info);
  KALDI_ASSERT(num_input_output_info >= 0);
  input_output_info.clear();
  ExpectToken(is, binary, "<InputOutputInfo>");
  for (size_t c = 0; c < num_input_output_info; c++) {
    int32 key;
    std::pair<int32, int32> val;
    ReadBasicType(is, binary, &key);
    ReadBasicType(is, binary, &(val.first));
    ReadBasicType(is, binary, &(val.second));
    input_output_info.insert(std::make_pair<int32, std::pair<int32, int32> >(key, val));
  }

  size_t num_commands;
  ExpectToken(is, binary, "<NumCommands>");
  ReadBasicType(is, binary, &num_commands);
  KALDI_ASSERT(num_commands >= 0);
  commands.resize(num_commands);
  ExpectToken(is, binary, "<Commands>");
  for (size_t c = 0; c < num_commands; c++) {
    commands[c].Read(is, binary);
  }

  ExpectToken(is, binary, "<NeedModelDerivative>");
  ReadBasicType(is, binary, &need_model_derivative);

  ComputeCudaIndexes();
  ExpectToken(is, binary, "</NnetComputation>");
}

void NnetComputation::Write(std::ostream &os, bool binary) const {
  WriteToken(os, binary, "<NnetComputation>");
  WriteToken(os, binary, "<NumMatrices>");
  WriteBasicType(os, binary, matrices.size());
  WriteToken(os, binary, "<Matrices>");
  for (size_t c = 0; c < matrices.size(); c++) {
    matrices[c].Write(os, binary);
  }

  if (!binary) os << std::endl;
  WriteToken(os, binary, "<NumMatrixDebugInfo>");
  WriteBasicType(os, binary, matrix_debug_info.size());
  WriteToken(os, binary, "<MatrixDebugInfo>");
  for (size_t c = 0; c < matrix_debug_info.size(); c++) {
    matrix_debug_info[c].Write(os, binary);
  }

  if (!binary) os << std::endl;
  WriteToken(os, binary, "<NumSubMatrices>");
  WriteBasicType(os, binary, submatrices.size());
  WriteToken(os, binary, "<SubMatrices>");
  for (size_t c = 0; c < submatrices.size(); c++) {
    submatrices[c].Write(os, binary);
  }
  
  if (!binary) os << std::endl;
  WriteToken(os, binary, "<NumIndexes>");
  WriteBasicType(os, binary, indexes.size());
  WriteToken(os, binary, "<Indexes>");
  for (size_t c = 0; c < indexes.size(); c++) {
    WriteIntegerVector(os, binary, indexes[c]);
  }

  if (!binary) os << std::endl;
  WriteToken(os, binary, "<NumIndexesMulti>");
  WriteBasicType(os, binary, indexes_multi.size());
  WriteToken(os, binary, "<IndexesMulti>");
  for (size_t c = 0; c < indexes_multi.size(); c++) {
    WriteIntegerPairVector(os, binary, indexes_multi[c]);
  }

  if (!binary) os << std::endl;
  WriteToken(os, binary, "<NumIndexesRanges>");
  WriteBasicType(os, binary, indexes_ranges.size());
  WriteToken(os, binary, "<IndexesRanges>");
  for (size_t c = 0; c < indexes_ranges.size(); c++) {
    WriteIntegerPairVector(os, binary, indexes_ranges[c]);
  }

  if (!binary) os << std::endl;
  WriteToken(os, binary, "<NumInputOutputInfo>");
  WriteBasicType(os, binary, input_output_info.size());
  WriteToken(os, binary, "<InputOutputInfo>");
  std::map<int32, std::pair<int32, int32> > input_output_info_cp(input_output_info.begin(), input_output_info.end());
  for (std::map<int32, std::pair<int32, int32> >::const_iterator iter =
           input_output_info_cp.begin(); iter != input_output_info_cp.end(); ++iter) {
    WriteBasicType(os, binary, iter->first);
    WriteBasicType(os, binary, iter->second.first);
    WriteBasicType(os, binary, iter->second.second);
  }

  if (!binary) os << std::endl;
  WriteToken(os, binary, "<NumCommands>");
  WriteBasicType(os, binary, commands.size());
  WriteToken(os, binary, "<Commands>");
  for (size_t c = 0; c < commands.size(); c++) {
    commands[c].Write(os, binary);
  }

  if (!binary) os << std::endl;
  WriteToken(os, binary, "<NeedModelDerivative>");
  WriteBasicType(os, binary, need_model_derivative);
  WriteToken(os, binary, "</NnetComputation>");
}

void NnetComputation::GetCommandStrings(
    const Nnet &nnet,
    std::string *preamble,
    std::vector<std::string> *command_strings) const {
  std::vector<std::string> submatrix_strings, indexes_strings,
      indexes_multi_strings;
  this->GetSubmatrixStrings(nnet, &submatrix_strings);
  GetIndexesStrings(nnet, *this, &indexes_strings);
  GetIndexesMultiStrings(nnet, *this, &indexes_multi_strings);
  if (preamble != NULL) {
    std::ostringstream os;
    PrintComputationPreamble(os, *this, nnet, submatrix_strings,
                             indexes_strings, indexes_multi_strings);
    *preamble = os.str();
  }
  if (command_strings != NULL) {
    command_strings->resize(commands.size());
    for (int32 c = 0; c < commands.size(); c++) {
      std::ostringstream os;
      PrintCommand(os, nnet, *this, c, submatrix_strings,
                   indexes_strings, indexes_multi_strings);
      (*command_strings)[c] = os.str();
      // Remove the final newline.
      std::string &str = (*command_strings)[c];
      if (!str.empty())
        str.resize(str.size() - 1);
    }
  }
}


bool NnetComputation::IsWholeMatrix(int32 submatrix_index) const {
  KALDI_ASSERT(submatrix_index > 0 && submatrix_index < submatrices.size());
  const SubMatrixInfo &submat_info = submatrices[submatrix_index];
  const MatrixInfo &mat_info = matrices[submat_info.matrix_index];
  return submat_info.row_offset == 0 && submat_info.col_offset == 0 &&
      submat_info.num_rows == mat_info.num_rows &&
      submat_info.num_cols == mat_info.num_cols;
}

bool NnetComputation::SubMatrixInfo::operator== (
    const NnetComputation::SubMatrixInfo &other) const {
  return matrix_index == other.matrix_index &&
      row_offset == other.row_offset &&
      num_rows == other.num_rows &&
      col_offset == other.col_offset &&
      num_cols == other.num_cols;
}

void IoSpecification::Print(std::ostream &os) const {
  os << "name=" << name << ", has-deriv=" << (has_deriv ? "true" : "false" )
     << ", indexes=";
  PrintIndexes(os, indexes);
  os << "\n";
}

void IoSpecification::Swap(IoSpecification *other) {
  name.swap(other->name);
  indexes.swap(other->indexes);
  std::swap(has_deriv, other->has_deriv);
}  

void IoSpecification::Read(std::istream &is, bool binary) {
  ExpectToken(is, binary, "<IoSpecification>");
  ReadToken(is, binary, &name);
  ExpectToken(is, binary, "<NumIndexes>");
  size_t num_indexes;
  ReadBasicType(is, binary, &num_indexes);
  ExpectToken(is, binary, "<Indexes>");
  ReadIndexVector(is, binary, &indexes);
  ExpectToken(is, binary, "<HasDeriv>");
  ReadBasicType(is, binary, &has_deriv);
  ExpectToken(is, binary, "</IoSpecification>");
}

void IoSpecification::Write(std::ostream &os, bool binary) const {
  WriteToken(os, binary, "<IoSpecification>");
  WriteToken(os, binary, name);
  WriteToken(os, binary, "<NumIndexes>");
  WriteBasicType(os, binary, indexes.size());
  WriteToken(os, binary, "<Indexes>");
  WriteIndexVector(os, binary, indexes);
  WriteToken(os, binary, "<HasDeriv>");
  WriteBasicType(os, binary, has_deriv);
  WriteToken(os, binary, "</IoSpecification>");
}

void ComputationRequest::Read(std::istream &is, bool binary) {
  ExpectToken(is, binary, "<ComputationRequest>");
  size_t num_inputs;
  ExpectToken(is, binary, "<NumInputs>");
  ReadBasicType(is, binary, &num_inputs);
  KALDI_ASSERT(num_inputs >= 0);
  inputs.resize(num_inputs);
  ExpectToken(is, binary, "<Inputs>");
  for (size_t c = 0; c < num_inputs; c++) {
    inputs[c].Read(is, binary);
  }

  size_t num_outputs;
  ExpectToken(is, binary, "<NumOutputs>");
  ReadBasicType(is, binary, &num_outputs);
  KALDI_ASSERT(num_outputs >= 0);
  outputs.resize(num_outputs);
  ExpectToken(is, binary, "<Outputs>");
  for (size_t c = 0; c < num_outputs; c++) {
    outputs[c].Read(is, binary);
  }

  ExpectToken(is, binary, "<NeedModelDerivative>");
  ReadBasicType(is, binary, &need_model_derivative);
  ExpectToken(is, binary, "<StoreComponentStats>");
  ReadBasicType(is, binary, &store_component_stats);
  ExpectToken(is, binary, "</ComputationRequest>");
}

void ComputationRequest::Write(std::ostream &os, bool binary) const {
  WriteToken(os, binary, "<ComputationRequest>");
  WriteToken(os, binary, "<NumInputs>");
  WriteBasicType(os, binary, inputs.size());
  WriteToken(os, binary, "<Inputs>");
  for (size_t c = 0; c < inputs.size(); c++) {
    inputs[c].Write(os, binary);
  }

  WriteToken(os, binary, "<NumOutputs>");
  WriteBasicType(os, binary, outputs.size());
  WriteToken(os, binary, "<Outputs>");
  for (size_t c = 0; c < outputs.size(); c++) {
    outputs[c].Write(os, binary);
  }

  WriteToken(os, binary, "<NeedModelDerivative>");
  WriteBasicType(os, binary, need_model_derivative);
  WriteToken(os, binary, "<StoreComponentStats>");
  WriteBasicType(os, binary, store_component_stats);
  WriteToken(os, binary, "</ComputationRequest>");
}
>>>>>>> added I/O interfaces for NnetComputation and ComputationRequest

void ComputationRequest::Print(std::ostream &os) const {
  os << " # Computation request:\n";
  for (size_t i = 0; i < inputs.size(); i++) {
    os << "input-" << i << ": ";
    inputs[i].Print(os);
  }
  for (size_t i = 0; i < outputs.size(); i++) {
    os << "output-" << i << ": ";
    outputs[i].Print(os);
  }
  os << "need-model-derivative: " <<
      (need_model_derivative ? "true\n" : "false\n");
  os << "store-component-stats: " <<
      (store_component_stats ? "true\n" : "false\n");
  misc_info.Print(os);
}

bool IoSpecification::operator== (const IoSpecification &other) const {
  return (name == other.name && indexes == other.indexes &&
          has_deriv == other.has_deriv);
}

IoSpecification::IoSpecification(const std::string &name,
                                 int32 t_start, int32 t_end):
    name(name), indexes(std::max<int32>(0, t_end - t_start)),
    has_deriv(false) {
  // the n and x values will already be 0 in "indexes" because
  // the default constructor does that; so just set the t values.
  std::vector<Index>::iterator iter = indexes.begin(), end = indexes.end();
  for (int32 t = t_start; iter != end; ++iter, ++t)
    iter->t = t;
}

bool ComputationRequest::operator== (const ComputationRequest &other) const {
  // rely on the std::vector's default implementation of ==, which in turn
  // relies on the == operator of class IoSpecification.
  return inputs == other.inputs && outputs == other.outputs &&
      need_model_derivative == other.need_model_derivative &&
      store_component_stats == other.store_component_stats &&
      misc_info == other.misc_info;
}

NnetComputation::NnetComputation(const NnetComputation &other):
    matrices(other.matrices),
    matrix_debug_info(other.matrix_debug_info),
    submatrices(other.submatrices),
    indexes(other.indexes),
    indexes_multi(other.indexes_multi),
    indexes_ranges(other.indexes_ranges),
    input_output_info(other.input_output_info),
    commands(other.commands),
    need_model_derivative(other.need_model_derivative),
    indexes_cuda(other.indexes_cuda),
    indexes_ranges_cuda(other.indexes_ranges_cuda) {
  for (size_t i = 0; i < other.component_precomputed_indexes.size(); i++)
      component_precomputed_indexes.push_back(
          other.component_precomputed_indexes[i] == NULL ? NULL :
          other.component_precomputed_indexes[i]->Copy());
}


NnetComputation& NnetComputation::operator = (const NnetComputation &other) {
    matrices = other.matrices;
    matrix_debug_info = other.matrix_debug_info;
    submatrices = other.submatrices;
    indexes = other.indexes;
    indexes_multi = other.indexes_multi;
    indexes_ranges = other.indexes_ranges;
    input_output_info = other.input_output_info;
    commands = other.commands;
    need_model_derivative = other.need_model_derivative;
    indexes_cuda = other.indexes_cuda;
    indexes_ranges_cuda = other.indexes_ranges_cuda;

    for (size_t i = 0; i < component_precomputed_indexes.size(); i++)
      delete component_precomputed_indexes[i];
    component_precomputed_indexes.clear();
    for (size_t i = 0; i < other.component_precomputed_indexes.size(); i++)
      component_precomputed_indexes.push_back(
          other.component_precomputed_indexes[i] == NULL ? NULL :
          other.component_precomputed_indexes[i]->Copy());
    return *this;
}

} // namespace nnet3
} // namespace kaldi

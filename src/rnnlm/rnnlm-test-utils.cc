// rnnlm/rnnlm-test-utils.cc

// Copyright 2017  Daniel Povey
//           2017  Hossein Hadian
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
#include "rnnlm/rnnlm-test-utils.h"

namespace kaldi {
namespace rnnlm {

void GetForbiddenSymbols(std::set<std::string> *forbidden_symbols) {
  *forbidden_symbols = {"<eps>", "<s>", "<brk>", "</s>"};
}

///  Reads all the lines from a text file and appends
///  them to the "sentences" vector.
void ReadAllLines(const std::string &filename,
                  std::vector<std::vector<std::string> > *sentences) {
  std::ifstream is(filename.c_str());
  std::string line;
  while (std::getline(is, line)) {
    std::vector<std::string> split_line;
    SplitStringToVector(line, "\t\r\n ", true, &split_line);
    sentences->push_back(split_line);
  }
  if (sentences->size() < 1)
    KALDI_ERR << "No line could be read from the file.";
}

void GetTestSentences(const std::set<std::string> &forbidden_symbols,
                      std::vector<std::vector<std::string> > *sentences) {
  sentences->clear();
  for (char i = '1'; i <= '5'; i++)
    ReadAllLines(i + std::string(".txt"), sentences);
  // find and escape forbidden symbols
  for (int i = 0; i < sentences->size(); i++)
    for (int j = 0; j < (*sentences)[i].size(); j++)
      if (forbidden_symbols.find((*sentences)[i][j]) != forbidden_symbols.end())
        (*sentences)[i][j] = "\\" + (*sentences)[i][j];
}

fst::SymbolTable *GetSymbolTable(
    const std::vector<std::vector<std::string> > &sentences) {
  fst::SymbolTable* table = new fst::SymbolTable();
  table->AddSymbol("<eps>", 0);
  table->AddSymbol("<s>", 1);
  table->AddSymbol("</s>", 2);
  table->AddSymbol("<brk>", 3);
  for (int i = 0; i < sentences.size(); i++)
    for (int j = 0; j < sentences[i].size(); j++)
      table->AddSymbol(sentences[i][j]);
  return table;
}

void ConvertToInteger(
    const std::vector<std::vector<std::string> > &string_sentences,
    const fst::SymbolTable &symbol_table,
    std::vector<std::vector<int32> > *int_sentences) {
  int_sentences->resize(string_sentences.size());
  for (int i = 0; i < string_sentences.size(); i++) {
    (*int_sentences)[i].resize(string_sentences[i].size());
    for (int j = 0; j < string_sentences[i].size(); j++) {
      kaldi::int64 key = symbol_table.Find(string_sentences[i][j]);
      KALDI_ASSERT(key != fst::SymbolTable::kNoSymbol);
      (*int_sentences)[i][j] = static_cast<int32>(key);
    }
  }
}


}  // namespace rnnlm
}  // namespace kaldi

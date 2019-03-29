// hmm/transitions.cc

// Copyright 2009-2012  Microsoft Corporation
//        Johns Hopkins University (author: Guoguo Chen)
//        2012-2019 Johns Hopkins University (Author: Daniel Povey)
//        2019      Hossein Hadian


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

#include <vector>
#include "hmm/transitions.h"
#include "tree/context-dep.h"
#include "util/common-utils.h"
#include "fstext/fstext-utils.h"

namespace kaldi {

bool Transitions::operator == (const Transitions &other) {
  return topo_ == other.topo_ && info_ == other.info_ &&
      num_pdfs_ == other.num_pdfs_;
}

void Transitions::ComputeInfo(const ContextDependencyInterface &ctx_dep) {
  using StateId = typename fst::StdFst::StateId;
  const std::vector<int32> &phones = topo_.GetPhones();
  KALDI_ASSERT(!phones.empty());

  // pdf_info is a set of lists indexed by phone. Each list is indexed by
  // (pdf-class, self-loop pdf-class) of each state of that phone, and the element
  // is a list of possible (pdf, self-loop pdf) pairs that that (pdf-class, self-loop pdf-class)
  // pair generates.
  std::vector<std::vector<std::vector<std::pair<int32, int32> > > > pdf_info;
  // pdf_class_pairs is a set of lists indexed by phone. Each list stores
  // (pdf-class, self-loop pdf-class) of each state of that phone.
  std::vector<std::vector<std::pair<int32, int32> > > pdf_class_pairs;
  pdf_class_pairs.resize(1 + *std::max_element(phones.begin(), phones.end()));

  std::vector<std::map<std::pair<int32, int32>, std::vector<std::pair<int32, int32> > > > to_topo_state_list;
  to_topo_state_list.resize(1 + *std::max_element(phones.begin(), phones.end()));

  for (size_t i = 0; i < phones.size(); i++) {
    int32 phone = phones[i];
    auto const &entry = topo_.TopologyForPhone(phone);  // an FST
    int num_states = entry.NumStates();

    std::vector<StateId> state_to_self_loop_pdf_class(num_states, -1);  // TODO(hhadian): Define and use kNoPdf
    for (StateId state = 0; state < num_states; ++state)
      for (fst::ArcIterator<fst::StdVectorFst> aiter(entry, state); !aiter.Done(); aiter.Next()) {
        const fst::StdArc &arc(aiter.Value());
        if (arc.nextstate == state) {
          KALDI_ASSERT(state_to_self_loop_pdf_class[state] == -1);  //kNoPdf Only 1 self-loop allowed.
          state_to_self_loop_pdf_class[state] = arc.ilabel;
        }
      }

    std::map<std::pair<int32, int32>, std::vector<std::pair<int32, int32> > > phone_to_topo_state_list;
    for (StateId state = 0; state < num_states; ++state) {
      for (fst::ArcIterator<fst::StdVectorFst> aiter(entry, state);
           !aiter.Done(); aiter.Next()) {
        const fst::StdArc &arc(aiter.Value());
        int32 forward_pdf_class = arc.ilabel,
            self_loop_pdf_class = state_to_self_loop_pdf_class[arc.nextstate];
        pdf_class_pairs[phone].push_back(std::make_pair(forward_pdf_class,
                                                        self_loop_pdf_class));
        auto state_arc_pair = std::make_pair(state, aiter.Position());
        auto pdf_class_pair = std::make_pair(forward_pdf_class, self_loop_pdf_class);
        phone_to_topo_state_list[pdf_class_pair].push_back(state_arc_pair);
      }
    }
    to_topo_state_list[phone] = phone_to_topo_state_list;
  }
  ctx_dep.GetPdfInfo(phones, pdf_class_pairs, &pdf_info);

  info_.push_back(TransitionIdInfo());  // transition-id is 1-based.

  for (int32 i = 0; i < phones.size(); i++) {
    int32 phone = phones[i];
    for (int32 j = 0; j < static_cast<int32>(pdf_info[phone].size()); j++) {
      int32 pdf_class = pdf_class_pairs[phone][j].first,
            self_loop_pdf_class = pdf_class_pairs[phone][j].second;
      auto const &state_arc_vec =
              to_topo_state_list[phone][std::make_pair(pdf_class, self_loop_pdf_class)];
      KALDI_ASSERT(!state_arc_vec.empty());
      for (auto const& state_arc_pair: state_arc_vec) {
        int32 topo_state = state_arc_pair.first,
            arc_index = state_arc_pair.second;
        for (size_t m = 0; m < pdf_info[phone][j].size(); m++) {
          int32 pdf = pdf_info[phone][j][m].first,
            self_loop_pdf = pdf_info[phone][j][m].second;
          TransitionIdInfo tuple{.phone = phone, .topo_state = topo_state,
                .arc_index = arc_index, .pdf_id = pdf, .self_loop_pdf_id = self_loop_pdf};
          info_.push_back(tuple);
        }
      }
    }
  }

  std::sort(info_.begin(), info_.end());  // sort to enable reverse lookup.
}

void Transitions::ComputeDerived() {
  pdf_ids_.resize(info_.size());
  for (int32 tid = 1; tid <= NumTransitionIds(); ++tid) {
    auto transition = info_[tid];
    auto const &entry = topo_.TopologyForPhone(transition.phone);  // an FST
    fst::ArcIterator<fst::StdVectorFst> aiter(entry, transition.topo_state);
    aiter.Seek(transition.arc_index);
    auto const &arc(aiter.Value());

    transition.is_self_loop = (arc.nextstate == transition.topo_state);
    transition.is_initial = (transition.topo_state == 0);
    transition.is_final = (entry.Final(arc.nextstate) != fst::StdFst::Weight::Zero());
    transition.transition_cost = arc.weight.Value();
    if (transition.self_loop_pdf_id == -1)
      transition.self_loop_transition_id = -1;
    else
      transition.self_loop_transition_id =
          TupleToTransitionId(transition.phone, transition.topo_state,
                              transition.arc_index, transition.self_loop_pdf_id,
                              transition.self_loop_pdf_id);

    pdf_ids_[tid] = transition.pdf_id;
  }
}

Transitions::Transitions(const ContextDependencyInterface &ctx_dep,
                             const Topology &topo): topo_(topo) {
  // First thing is to get all possible tuples.
  ComputeInfo(ctx_dep);
  ComputeDerived();
  Check();
}

int32 Transitions::TupleToTransitionId(int32 phone, int32 topo_state,
                                       int32 arc_index, int32 pdf_id,
                                       int32 self_loop_pdf_id) const {
  TransitionIdInfo tuple{.phone = phone, .topo_state = topo_state,
        .arc_index = arc_index, .pdf_id = pdf_id, .self_loop_pdf_id = self_loop_pdf_id};
  // Note: if this ever gets too expensive, which is unlikely, we can refactor
  // this code to sort first on pdf, and then index on pdf, so those
  // that have the same pdf are in a contiguous range.
  auto lowerbound = std::lower_bound(info_.begin(), info_.end(), tuple);
  if (lowerbound == info_.end() || !(*lowerbound == tuple))
    KALDI_ERR << "Tuple not found. (incompatible tree and model?)";

  return static_cast<int32>((lowerbound - info_.begin()));
}

void Transitions::Read(std::istream &is, bool binary) {
  ExpectToken(is, binary, "<Transitions>");
  topo_.Read(is, binary);
  ExpectToken(is, binary, "<Info>");
  int32 size;
  ReadBasicType(is, binary, &size);
  info_.resize(size);
  for (int32 i = 0; i < size; i++) {
    ReadBasicType(is, binary, &(info_[i].phone));
    ReadBasicType(is, binary, &(info_[i].topo_state));
    ReadBasicType(is, binary, &(info_[i].arc_index));
    ReadBasicType(is, binary, &(info_[i].pdf_id));
    ReadBasicType(is, binary, &(info_[i].self_loop_pdf_id));
  }
  ExpectToken(is, binary, "</Info>");
  ExpectToken(is, binary, "</Transitions>");
  ComputeDerived();
  Check();
}

void Transitions::Write(std::ostream &os, bool binary) const {
  WriteToken(os, binary, "<Transitions>");
  if (!binary) os << "\n";
  topo_.Write(os, binary);
  WriteToken(os, binary, "<Info>");
  WriteBasicType(os, binary, static_cast<int32>(info_.size()));
  if (!binary) os << "\n";
  for (int32 i = 0; i < static_cast<int32> (info_.size()); i++) {
    WriteBasicType(os, binary, info_[i].phone);
    WriteBasicType(os, binary, info_[i].topo_state);
    WriteBasicType(os, binary, info_[i].arc_index);
    WriteBasicType(os, binary, info_[i].pdf_id);
    WriteBasicType(os, binary, info_[i].self_loop_pdf_id);
    if (!binary) os << "\n";
  }
  WriteToken(os, binary, "</Info>");
  if (!binary) os << "\n";
  WriteToken(os, binary, "</Transitions>");
  if (!binary) os << "\n";
}

void Transitions::Check() const {

}

void Transitions::Print(std::ostream &os,
                            const std::vector<std::string> &phone_names,
                            const Vector<double> *occs) {
}

bool GetPdfsForPhones(const Transitions &trans_model,
                      const std::vector<int32> &phones,
                      std::vector<int32> *pdfs) {
  return true;
}

bool GetPhonesForPdfs(const Transitions &trans_model,
                     const std::vector<int32> &pdfs,
                     std::vector<int32> *phones) {
  return true;
}


} // End namespace kaldi

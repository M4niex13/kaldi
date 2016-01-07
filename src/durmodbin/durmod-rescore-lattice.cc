// durmodbin/durmod-rescore-lattice.cc

// Copyright 2015 Hossein Hadian

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

#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "fstext/fstext-lib.h"
#include "lat/kaldi-lattice.h"
#include "lat/lattice-functions.h"
#include "hmm/transition-model.h"
#include "durmod/kaldi-durmod.h"

int main(int argc, char *argv[]) {
  try {
    using namespace kaldi;
    typedef kaldi::int32 int32;
    using fst::SymbolTable;
    using fst::VectorFst;
    using kaldi::CompactLatticeArc;

    const char *usage =
      "Rescore a lattice using the scores from a phone duration model.\n"
      "Usage:  durmod-rescore-lattice [options] <dur-model> <trans-model> "
      "<lattice-rspecifier> <lattice-wspecifier>\n"
      "e.g.: \n"
      "durmod-rescore-lattice durmodel.mdl final.mdl "
      "ark:lat.1 ark:rescored_lat.1\n";

    BaseFloat lm_scale = 1.0;
    ParseOptions po(usage);
    po.Register("lm-scale", &lm_scale, "Scaling factor for language model "
                "costs");

    po.Read(argc, argv);

    if (po.NumArgs() != 4) {
      po.PrintUsage();
      exit(1);
    }

    TransitionModel trans_model;
    std::string durmodel_filename = po.GetArg(1),
                model_filename = po.GetArg(2),
                lats_rspecifier = po.GetArg(3),
                lats_wspecifier = po.GetOptArg(4);

    ReadKaldiObject(model_filename, &trans_model);
    PhoneDurationModel durmodel;
    ReadKaldiObject(durmodel_filename, &durmodel);

    PhoneDurationScoreComputer durmodel_scorer(durmodel);

    // Read and write as compact lattice.
    SequentialCompactLatticeReader compact_lattice_reader(lats_rspecifier);
    CompactLatticeWriter compact_lattice_writer(lats_wspecifier);

    int32 n_done = 0, n_fail = 0;
    for (; !compact_lattice_reader.Done(); compact_lattice_reader.Next()) {
      std::string key = compact_lattice_reader.Key();
      CompactLattice clat = compact_lattice_reader.Value();
      compact_lattice_reader.FreeCurrent();
      KALDI_LOG << "Rescoring lattice for key " << key;

      if (lm_scale != 0.0) {

        fst::ScaleLattice(fst::GraphLatticeScale(1.0 / lm_scale), &clat);
        ArcSort(&clat, fst::OLabelCompare<CompactLatticeArc>());

        // Insert the phone-id/duration info into the lattice olabels
        DurationModelReplaceLabelsLattice(&clat,
                                          trans_model,
                                          durmodel.MaxDuration());

        // Wrap the duration-model scorer with an on-demand fst
        PhoneDurationModelDeterministicFst on_demand_fst(durmodel,
                                                         &durmodel_scorer);

        // Compose the lattice with the on-demand fst
        CompactLattice composed_clat;
        ComposeCompactLatticeDeterministic(clat,
                                           &on_demand_fst,
                                           &composed_clat);
        // Replace the labels back
        DurationModelReplaceLabelsBackLattice(&clat);

        // Determinizes the composed lattice.
        Lattice composed_lat;
        ConvertLattice(composed_clat, &composed_lat);
        Invert(&composed_lat);
        CompactLattice determinized_clat;
        DeterminizeLattice(composed_lat, &determinized_clat);
        fst::ScaleLattice(fst::GraphLatticeScale(lm_scale), &determinized_clat);
        if (determinized_clat.Start() == fst::kNoStateId) {
          KALDI_WARN << "Empty lattice for utterance " << key;
          n_fail++;
        } else {
          compact_lattice_writer.Write(key, determinized_clat);
          n_done++;
        }
      } else {
        // Zero scale so nothing to do.
        n_done++;
        compact_lattice_writer.Write(key, clat);
      }
    }

    KALDI_LOG << "Rescored " << n_done << " lattices with " << n_fail
      << " failures.";
    return 0;
  } catch(const std::exception &e) {
    std::cerr << e.what();
    return -1;
  }
}


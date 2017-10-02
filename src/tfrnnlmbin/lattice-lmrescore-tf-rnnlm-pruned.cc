// tfrnnlmbin/lattice-lmrescore-tf-rnnlm.cc

// Copyright (C) 2017 Intellisist, Inc. (Author: Hainan Xu)

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
#include "fstext/fstext-lib.h"
#include "tfrnnlm/tensorflow-rnnlm.h"
#include "util/common-utils.h"
#include "lat/kaldi-lattice.h"
#include "lat/lattice-functions.h"
#include "lat/compose-lattice-pruned.h"

fst::VectorFst<fst::StdArc> *ReadAndPrepareLmFst(std::string rxfilename) {
  // ReadFstKaldi() will die with exception on failure.
  fst::VectorFst<fst::StdArc> *ans = fst::ReadFstKaldi(rxfilename);
  if (ans->Properties(fst::kAcceptor, true) == 0) {
    // If it's not already an acceptor, project on the output, i.e. copy olabels
    // to ilabels.  Generally the G.fst's on disk will have the disambiguation
    // symbol #0 on the input symbols of the backoff arc, and projection will
    // replace them with epsilons which is what is on the output symbols of
    // those arcs.
    fst::Project(ans, fst::PROJECT_OUTPUT);
  }
  if (ans->Properties(fst::kILabelSorted, true) == 0) {
    // Make sure LM is sorted on ilabel.
    fst::ILabelCompare<fst::StdArc> ilabel_comp;
    fst::ArcSort(ans, ilabel_comp);
  }
  return ans;
}

int main(int argc, char *argv[]) {
  try {
    using namespace kaldi;
    using namespace kaldi::tf_rnnlm;
    typedef kaldi::int32 int32;
    typedef kaldi::int64 int64;
    using fst::SymbolTable;
    using fst::VectorFst;
    using fst::StdArc;
    using fst::ReadFstKaldi;
    using std::unique_ptr;

    const char *usage =
        "Rescores lattice with rnnlm that is trained with TensorFlow.\n"
        "An example script for training and rescoring with the TensorFlow\n"
        "RNNLM is at egs/ami/s5/local/tfrnnlm/run_lstm.sh\n"
        "\n"
        "Usage: lattice-lmrescore-tf-rnnlm [options] [unk-file] <rnnlm-wordlist> \\\n"
        "             <word-symbol-table-rxfilename> <lattice-rspecifier> \\\n"
        "             <rnnlm-rxfilename> <lattice-wspecifier>\n"
        " e.g.: lattice-lmrescore-tf-rnnlm --lm-scale=-1.0 unkcounts.txt rnnwords.txt \\\n"
        "              words.txt ark:in.lats rnnlm ark:out.lats\n";

    ParseOptions po(usage);
    int32 max_ngram_order = 3;
    BaseFloat lm_scale = 1.0;
    BaseFloat acoustic_scale = 0.1;

    po.Register("lm-scale", &lm_scale, "Scaling factor for <lm-to-add>; its negative "
                "will be applied to <lm-to-subtract>.");
    po.Register("acoustic-scale", &acoustic_scale, "Scaling factor for acoustic "
                "probabilities (e.g. 0.1 for non-chain systems); important because "
                "of its effect on pruning.");
    po.Register("max-ngram-order", &max_ngram_order,
        "If positive, allow RNNLM histories longer than this to be identified "
        "with each other for rescoring purposes (an approximation that "
        "saves time and reduces output lattice size).");

    KaldiTfRnnlmWrapperOpts opts;
    ComposeLatticePrunedOptions compose_opts;
    opts.Register(&po);
    compose_opts.Register(&po);

    po.Read(argc, argv);

    if (po.NumArgs() != 7 && po.NumArgs() != 6) {
      po.PrintUsage();
      exit(1);
    }

    std::string lm_to_subtract_rxfilename, lats_rspecifier, rnn_word_list,
      word_symbols_rxfilename, rnnlm_rxfilename, lats_wspecifier, unk_prob_file;
    if (po.NumArgs() == 6) {
      lm_to_subtract_rxfilename = po.GetArg(1),
      word_symbols_rxfilename = po.GetArg(2);
      rnn_word_list = po.GetArg(3);
      rnnlm_rxfilename = po.GetArg(4);
      lats_rspecifier = po.GetArg(5);
      lats_wspecifier = po.GetArg(6);
    } else {
      lm_to_subtract_rxfilename = po.GetArg(1),
      word_symbols_rxfilename = po.GetArg(2);
      unk_prob_file = po.GetArg(3);
      rnn_word_list = po.GetArg(4);
      rnnlm_rxfilename = po.GetArg(5);
      lats_rspecifier = po.GetArg(6);
      lats_wspecifier = po.GetArg(7);
    }

    KALDI_LOG << "Reading old LMs...";
    VectorFst<StdArc> *lm_to_subtract_fst = ReadAndPrepareLmFst(
        lm_to_subtract_rxfilename);
    fst::BackoffDeterministicOnDemandFst<StdArc> lm_to_subtract_det_backoff(
        *lm_to_subtract_fst);
    fst::ScaleDeterministicOnDemandFst lm_to_subtract_det_scale(
        -lm_scale, &lm_to_subtract_det_backoff);

    // Reads the TF language model.
    KaldiTfRnnlmWrapper rnnlm(opts, rnn_word_list, word_symbols_rxfilename,
                                unk_prob_file, rnnlm_rxfilename);

    // Reads and writes as compact lattice.
    SequentialCompactLatticeReader compact_lattice_reader(lats_rspecifier);
    CompactLatticeWriter compact_lattice_writer(lats_wspecifier);

    int32 n_done = 0, n_fail = 0;

    TfRnnlmDeterministicFst* lm_to_add_orig = 
      new TfRnnlmDeterministicFst(max_ngram_order, &rnnlm);

    for (; !compact_lattice_reader.Done(); compact_lattice_reader.Next()) {
      fst::DeterministicOnDemandFst<StdArc> *lm_to_add =
         new fst::ScaleDeterministicOnDemandFst(lm_scale, lm_to_add_orig);

      std::string key = compact_lattice_reader.Key();
      CompactLattice clat = compact_lattice_reader.Value();
      compact_lattice_reader.FreeCurrent();

      // Before composing with the LM FST, we scale the lattice weights
      // by the inverse of "lm_scale".  We'll later scale by "lm_scale".
      // We do it this way so we can determinize and it will give the
      // right effect (taking the "best path" through the LM) regardless
      // of the sign of lm_scale.
      if (acoustic_scale != 1.0) {
        fst::ScaleLattice(fst::AcousticLatticeScale(acoustic_scale), &clat);
      }
      TopSortCompactLatticeIfNeeded(&clat);

      fst::ComposeDeterministicOnDemandFst<StdArc> combined_lms(
          &lm_to_subtract_det_scale, lm_to_add);

      // Composes lattice with language model.
      CompactLattice composed_clat;
      ComposeCompactLatticePruned(compose_opts, clat,
                                  &combined_lms, &composed_clat);
      lm_to_add_orig->Clear();

      if (composed_clat.NumStates() == 0) {
        // Something went wrong.  A warning will already have been printed.
        n_fail++;
      } else {
        if (acoustic_scale != 1.0) {
          if (acoustic_scale == 0.0)
            KALDI_ERR << "Acoustic scale cannot be zero.";
          fst::ScaleLattice(fst::AcousticLatticeScale(1.0 / acoustic_scale),
                            &composed_clat);
        }
        compact_lattice_writer.Write(key, composed_clat);
        n_done++;
      }
      delete lm_to_add;
    }
    delete lm_to_subtract_fst;
    delete lm_to_add_orig;

    KALDI_LOG << "Done " << n_done << " lattices, failed for " << n_fail;
    return (n_done != 0 ? 0 : 1);
  } catch(const std::exception &e) {
    std::cerr << e.what();
    return -1;
  }
}

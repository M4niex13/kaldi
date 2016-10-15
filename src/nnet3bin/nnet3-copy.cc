// nnet3bin/nnet3-copy.cc

// Copyright 2012  Johns Hopkins University (author:  Daniel Povey)
//           2015  Xingyu Na

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

#include <typeinfo>
#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "hmm/transition-model.h"
#include "nnet3/am-nnet-simple.h"
#include "nnet3/nnet-utils.h"

int main(int argc, char *argv[]) {
  try {
    using namespace kaldi;
    using namespace kaldi::nnet3;
    typedef kaldi::int32 int32;

    const char *usage =
        "Copy 'raw' nnet3 neural network to standard output\n"
        "Also supports setting all the learning rates to a value\n"
        "(the --learning-rate option)\n"
        "\n"
        "Usage:  nnet3-copy [options] <nnet-in> <nnet-out>\n"
        "e.g.:\n"
        " nnet3-copy --binary=false 0.raw text.raw\n";

    bool binary_write = true;
    BaseFloat learning_rate = -1,
      dropout = 0.0;
    std::string nnet_config, edits_config, edits_str;

    ParseOptions po(usage);
    po.Register("binary", &binary_write, "Write output in binary mode");
    po.Register("learning-rate", &learning_rate,
                "If supplied, all the learning rates of updatable components"
                "are set to this value.");
    po.Register("nnet-config", &nnet_config,
                "Name of nnet3 config file that can be used to add or replace "
                "components or nodes of the neural network (the same as you "
                "would give to nnet3-init).");
    po.Register("edits-config", &edits_config,
                "Name of edits-config file that can be used to modify the network "
                "(applied after nnet-config).  See comments for ReadEditConfig()"
                "in nnet3/nnet-utils.h to see currently supported commands.");
    po.Register("edits", &edits_str,
                "Can be used as an inline alternative to edits-config; semicolons "
                "will be converted to newlines before parsing.  E.g. "
                "'--edits=remove-orphans'.");
    po.Register("set-dropout-proportion", &dropout, "Set dropout proportion "
                "in all DropoutComponent to this value.");

    WriteKaldiObject(nnet, raw_nnet_wxfilename, binary_write);
    KALDI_LOG << "Copied raw neural net from " << raw_nnet_rxfilename
              << " to " << raw_nnet_wxfilename;

    return 0;
  } catch(const std::exception &e) {
    std::cerr << e.what() << '\n';
    return -1;
  }
}

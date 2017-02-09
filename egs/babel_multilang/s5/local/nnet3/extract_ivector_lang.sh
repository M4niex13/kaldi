#!/bin/bash
# This scripts extract iVector using global iVector extractor
# trained on all languages in multilingual setup.

. ./cmd.sh
set -e
stage=1
train_set=train
global_extractor=exp/multi/nnet3/extractor
ivector_suffix=_gb

[ ! -f ./conf/common_vars.sh ] && echo 'the file conf/common_vars.sh does not exist!' && exit 1

. conf/common_vars.sh || exit 1;

[ -f local.conf ] && . ./local.conf

. ./utils/parse_options.sh

lang=$1

mkdir -p nnet3

if [ $stage -le 8 ]; then
  # We extract iVectors on all the train_nodup data, which will be what we
  # train the system on.
  
  # having a larger number of speakers is helpful for generalization, and to
  # handle per-utterance decoding well (iVector starts at zero).
  steps/online/nnet2/copy_data_dir.sh --utts-per-spk-max 2 data/$lang/${train_set}_hires data/$lang/${train_set}_max2_hires
  
  if [ ! -f exp/$lang/nnet3/ivectors_${train_set}${ivector_suffix}/ivector_online.scp ]; then
    steps/online/nnet2/extract_ivectors_online.sh --cmd "$train_cmd" --nj 200 \
      data/$lang/${train_set}_max2_hires $global_extractor exp/$lang/nnet3/ivectors_${train_set}${ivector_suffix} || exit 1;
  fi

fi


exit 0;

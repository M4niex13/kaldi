#!/usr/bin/env bash
# (Siyuan Feng) based on babel/s5d/local/chain/run_ivector_common.sh

set -eu -o pipefail


# This script is called from local/nnet3/run_tdnn.sh and local/chain/run_tdnn.sh (and may eventually
# be called by more scripts).  It contains the common feature preparation and iVector-related parts
# of the script.  See those scripts for examples of usage.

# BABEL TRAIN:
# Amharic - 307
# Bengali - 103
# Cantonese - 101
# Javanese - 402
# Vietnamese - 107
# Zulu - 206
# BABEL TEST:
# Georgian - 404
# Lao - 203
#babel_langs="307 103 101 402 107 206 404 203"
babel_langs=""
babel_recog="${babel_langs}"
data_aug_suffix= # set to null, whereas typically kaldi uses _sp

#GlobalPhone:
#Czech       S0196
#French      S0197
#Spanish     S0203
#mandarin    S0193
#Thai        S0321
gp_langs="Czech French Mandarin Spanish Thai"
gp_recog="${gp_langs}"

stage=0
stop_stage=1
nj=30
diag_ubm_train_nj=30
ivec_train_nj=10
extract_ivec_nj=30

#train_set=train_cleaned   # you might set this to e.g. train.
#gmm=tri5_cleaned          # This specifies a GMM-dir from the features
                          # of the type you're training the system on;
                          # it should contain alignments for 'train_set'.
#langdir=data/langp/tri5_ali
num_threads_ubm=12
nnet3_affix= #_cleaned

. ./cmd.sh
. ./path.sh

#[ ! -f ./lang.conf ] && \
#  echo 'Language configuration does not exist! Use the configurations in conf/lang/* as a startup' && exit 1
#[ ! -f ./conf/common_vars.sh ] && \
#  echo 'the file conf/common_vars.sh does not exist!' && exit 1

#. ./conf/common_vars.sh || exit 1;
#. ./lang.conf || exit 1;

#[ -f ./local.conf ] && . ./local.conf

. ./utils/parse_options.sh


train_set=""
#train_set_hires=""
dev_set=""
#dev_set_hires=""
for l in  ${babel_langs}; do
  train_set="$l/data/train_${l} ${train_set}"
#  train_set_hires="$l/data_mfcc_hires/train_${l} ${train_set_hires}"

  dev_set="$l/data/dev_${l} ${dev_set}"
#  dev_set_hires="$l/data_mfcc_hires/dev_${l} ${dev_set_hires}"
done
for l in ${gp_langs}; do
  train_set="GlobalPhone/gp_${l}_train ${train_set}"
  dev_set="GlobalPhone/gp_${l}_dev ${dev_set}"
done
train_set=${train_set%% }
#train_set_hires=${train_set_hires%% }

dev_set=${dev_set%% }
#dev_set_hires=${dev_set_hires%% }

recog_set=""
for l in ${babel_recog} ${gp_recog}; do
  recog_set="eval_${l} ${recog_set}"
done
recog_set=${recog_set%% }

echo "Training data directories: ${train_set[*]}"
echo "Dev data directories: ${dev_set[*]}"
echo "Eval data directories: ${recog_set[*]}"

full_train_set=train
full_dev_set=dev

function langname() {
  # Utility
  echo "$(basename "$1")"
}
#gmm_dir=exp/${gmm}
#ali_dir=exp/${gmm}_ali_${train_set}_sp

#for f in data/${train_set}/feats.scp ${gmm_dir}/final.mdl; do
#  if [ ! -f $f ]; then
#    echo "$0: expected file $f to exist"
#    exit 1
#  fi
#done

# Siyuan: don't do data augmentation at this stage
#if [ $stage -le 1 ]; then
#  # Although the nnet will be trained by high resolution data, we still have to
#  # perturb the normal data to get the alignment _sp stands for speed-perturbed
#  echo "$0: preparing directory for low-resolution speed-perturbed data (for alignment)"
#  utils/data/perturb_data_dir_speed_3way.sh data/${train_set} data/${train_set}_sp
#  echo "$0: making PLP features for low-resolution speed-perturbed data"
#  steps/make_plp_pitch.sh --cmd "$train_cmd" --nj $nj data/${train_set}_sp
#  steps/compute_cmvn_stats.sh data/${train_set}_sp
#  utils/fix_data_dir.sh data/${train_set}_sp
#fi
#
#if [ $stage -le 2 ]; then
#  echo "$0: aligning with the perturbed low-resolution data"
#  steps/align_fmllr.sh --nj $nj --cmd "$train_cmd" \
#    data/${train_set}_sp data/lang $gmm_dir $ali_dir || exit 1
#fi
#
if [ $stage -le 3 ] && [ $stop_stage -gt 3  ]   ; then
  echo "$0: creating high-resolution MFCC features"
  if [[ $(hostname -f) == *.clsp.jhu.edu ]] && [ ! -d $mfccdir/storage ]; then
    utils/create_split_dir.pl \
      /export/b0{5,6,7,8}/$USER/kaldi-data/mfcc/babel-$(date +'%m_%d_%H_%M')/s5d/$RANDOM/$mfccdir/storage $mfccdir/storage
  fi

#  utils/data/perturb_data_dir_volume.sh data/${train_set}${data_aug_suffix}_hires

  for datadir in ${train_set}${data_aug_suffix} ; do
    utils/copy_data_dir.sh data/$datadir data/${datadir}${data_aug_suffix}_hires
   mfccdir=mfcc${data_aug_suffix}_hires
    steps/make_mfcc_pitch.sh --nj $nj --mfcc-config conf/mfcc_hires.conf \
      --cmd "$train_cmd" \
      data/${datadir}${data_aug_suffix}_hires exp/make_mfcc_hires/${datadir} $mfccdir
    steps/compute_cmvn_stats.sh data/${datadir}${data_aug_suffix}_hires
    utils/fix_data_dir.sh data/${datadir}${data_aug_suffix}_hires

    utils/data/limit_feature_dim.sh 0:39 \
      data/${datadir}${data_aug_suffix}_hires data/${datadir}${data_aug_suffix}_hires_nopitch || exit 1;
    steps/compute_cmvn_stats.sh \
      data/${datadir}${data_aug_suffix}_hires_nopitch exp/make_mfcc_hires/${datadir}_nopitch $mfccdir || exit 1;
    utils/fix_data_dir.sh data/${datadir}${data_aug_suffix}_hires_nopitch

  done
fi

if [ $stage -le 4 ] && [ $stop_stage -gt 4  ]; then
  echo "$0: computing a subset of data to train the diagonal UBM."

 for data_dir in ${train_set}${data_aug_suffix};do
    lang_name=$(langname $data_dir)    mkdir -p exp/nnet3${nnet3_affix}/$lang_name/diag_ubm
    temp_data_root=exp/nnet3${nnet3_affix}/$lang_name/diag_ubm

    # train a diagonal UBM using a subset of about a quarter of the data
    # we don't use the _comb data for this as there is no need for compatibility with
    # the alignments, and using the non-combined data is more efficient for I/O
    # (no messing about with piped commands).
    num_utts_total=$(wc -l <data/${data_dir}${data_aug_suffix}_hires/utt2spk)
    if [ $num_utts_total -gt 14000 ] ; then
      num_utts=14000
    else
      num_utts=$num_utts_total
    fi
    utils/data/subset_data_dir.sh data/${data_dir}${data_aug_suffix}_hires_nopitch \
      $num_utts ${temp_data_root}/${data_dir}${data_aug_suffix}_hires_nopitch_subset

    echo "$0: computing a PCA transform from the hires data."
    steps/online/nnet2/get_pca_transform.sh --cmd "$train_cmd" \
        --splice-opts "--left-context=3 --right-context=3" \
        --max-utts 10000 --subsample 2 \
         ${temp_data_root}/${data_dir}${data_aug_suffix}_hires_nopitch_subset \
         exp/nnet3${nnet3_affix}/$lang_name/pca_transform

    echo "$0: training the diagonal UBM."
    # Use 512 Gaussians in the UBM.
    steps/online/nnet2/train_diag_ubm.sh --cmd "$train_cmd" --nj $diag_ubm_train_nj \
      --num-frames 700000 \
      --num-threads $num_threads_ubm \
      ${temp_data_root}/${data_dir}${data_aug_suffix}_hires_nopitch_subset 512 \
      exp/nnet3${nnet3_affix}/$lang_name/pca_transform exp/nnet3${nnet3_affix}/$lang_name/diag_ubm
  done
fi

if [ $stage -le 5 ] && [ $stop_stage -gt 5  ]; then
  # Train the iVector extractor.  Use all of the speed-perturbed data since iVector extractors
  # can be sensitive to the amount of data.  The script defaults to an iVector dimension of
  # 100.
  for data_dir in ${train_set}${data_aug_suffix} ; do
    echo "$0: training the iVector extractor"
    lang_name=$(langname $data_dir)    steps/online/nnet2/train_ivector_extractor.sh --cmd "$train_cmd" --nj $ivec_train_nj \
      data/${data_dir}${data_aug_suffix}_hires_nopitch exp/nnet3${nnet3_affix}/$lang_name/diag_ubm \
      exp/nnet3${nnet3_affix}/$lang_name/extractor || exit 1;
  done
fi

if [ $stage -le 6 ] && [ $stop_stage -gt 6  ]; then
  # note, we don't encode the 'max2' in the name of the ivectordir even though
  # that's the data we extract the ivectors from, as it's still going to be
  # valid for the non-'max2' data, the utterance list is the same.

  for data_dir in ${train_set}${data_aug_suffix} ; do
    lang_name=$(langname $data_dir)
    ivectordir=exp/nnet3${nnet3_affix}/$lang_name/ivectors${data_aug_suffix}_hires
    if [[ $(hostname -f) == *.clsp.jhu.edu ]] && [ ! -d $ivectordir/storage ]; then
      utils/create_split_dir.pl \
        /export/b0{5,6,7,8}/$USER/kaldi-data/ivectors/babel-$(date +'%m_%d_%H_%M')/s5d/$RANDOM/$ivectordir/storage $ivectordir/storage
    fi
    # We extract iVectors on the speed-perturbed training data after combining
    # short segments, which will be what we train the system on.  With
    # --utts-per-spk-max 2, the script pairs the utterances into twos, and treats
    # each of these pairs as one speaker; this gives more diversity in iVectors..
    # Note that these are extracted 'online'.

    # having a larger number of speakers is helpful for generalization, and to
    # handle per-utterance decoding well (iVector starts at zero).
    temp_data_root=${ivectordir}
    utils/data/modify_speaker_info.sh --utts-per-spk-max 2 \
      data/${data_dir}${data_aug_suffix}_hires_nopitch \
      ${temp_data_root}/${data_dir}${data_aug_suffix}_hires_nopitch_max2

    steps/online/nnet2/extract_ivectors_online.sh --cmd "$train_cmd" --nj $extract_ivec_nj \
      ${temp_data_root}/${data_dir}${data_aug_suffix}_hires_nopitch_max2 \
      exp/nnet3${nnet3_affix}/$lang_name/extractor $ivectordir
  done
fi

echo "iVector preparation done."
exit 0
    
    

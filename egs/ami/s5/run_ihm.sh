#!/bin/bash -u

. ./cmd.sh
. ./path.sh

# To run recipe you need : a) SRILM, 

# Do not change this, it's for ctr-c ctr-v of training commands between ihm, sdm and mdm
mic=ihm

# Path where AMI gets downloaded (or where locally available):
#AMI_DIR=/disk/data2/amicorpus/ # Edinburgh,
AMI_DIR=$PWD/DOWNLOAD/amicorpus/ # BUT,

# Path to Fisher transcripts LM interpolation (if not defined only AMI transcript LM is built),
#FISHER_TRANS=`pwd`/eddie_data/lm/data/fisher/part1 # Edinburgh,
FISHER_TRANS=/mnt/matylda2/data/FISHER/fe_03_p1_tran # BUT,

stage=0
. utils/parse_options.sh

# Set bash to 'debug' mode, it will exit on : 
# -e 'error', -u 'undefined variable', -o ... 'error in pipeline', -x 'print commands',
set -e
set -u
set -o pipefail
set -x

# 1)
# In case you want download AMI corpus, uncomment this line.
# You need arount 130GB of free space to get whole data ihm+mdm
if [ $stage -le 0 ]; then
  local/ami_download.sh ihm $AMI_DIR
fi


# 2) Data preparation
if [ $stage -le 1 ]; then
  local/ami_text_prep.sh $AMI_DIR

  local/ami_ihm_data_prep.sh $AMI_DIR

  local/ami_ihm_scoring_data_prep.sh $AMI_DIR dev

  local/ami_ihm_scoring_data_prep.sh $AMI_DIR eval

  local/ami_prepare_dict.sh

  utils/prepare_lang.sh data/local/dict "<unk>" data/local/lang data/lang

  local/ami_train_lms.sh --fisher $FISHER_TRANS data/ihm/train/text data/ihm/dev/text data/local/dict/lexicon.txt data/local/lm

  final_lm=`cat data/local/lm/final_lm`
  LM=$final_lm.pr1-7

  prune-lm --threshold=1e-7 data/local/lm/$final_lm.gz /dev/stdout | \
    gzip -c > data/local/lm/$LM.gz

  utils/format_lm.sh data/lang data/local/lm/$LM.gz data/local/dict/lexicon.txt data/lang_$LM
fi


# 3) Building systems
# here starts the normal recipe, which is mostly shared across mic scenarios,
# - for ihm we adapt to speaker by fMLLR,
# - for sdm and mdm we do not adapt for speaker, but for environment only (cmn),

mfccdir=mfcc_$mic
if [ $stage -le 2 ]; then
  for dset in train dev eval; do
    steps/make_mfcc.sh --nj 15 --cmd "$train_cmd" data/$mic/$dset exp/$mic/make_mfcc/$dset $mfccdir
    steps/compute_cmvn_stats.sh data/$mic/$dset exp/$mic/make_mfcc/$dset $mfccdir
  done
  for dset in train eval dev; do utils/fix_data_dir.sh data/$mic/$dset; done
fi

# 4) Train systems
nj=30 # number of parallel jobs,

if [ $stage -le 3 ]; then
  # Taking a subset, accelerates the initial steps, we take ~20% of shortest sentences,
  utils/subset_data_dir.sh --shortest data/$mic/train 10000 data/$mic/train_10k
fi

if [ $stage -le 4 ]; then
  # Mono,
  steps/train_mono.sh --nj $nj --cmd "$train_cmd" --cmvn-opts "--norm-means=true --norm-vars=false" \
    data/$mic/train_10k data/lang exp/$mic/mono
  steps/align_si.sh --nj $nj --cmd "$train_cmd" \
    data/$mic/train_10k data/lang exp/$mic/mono exp/$mic/mono_ali

  # Deltas,
  steps/train_deltas.sh --cmd "$train_cmd" --cmvn-opts "--norm-means=true --norm-vars=false" \
    3000 40000 data/$mic/train_10k data/lang exp/$mic/mono_ali exp/$mic/tri1
  steps/align_si.sh --nj $nj --cmd "$train_cmd" \
    data/$mic/train data/lang exp/$mic/tri1 exp/$mic/tri1_ali
fi

if [ $stage -le 5 ]; then
  # Deltas again,
  steps/train_deltas.sh --cmd "$train_cmd" --cmvn-opts "--norm-means=true --norm-vars=false" \
    5000 80000 data/$mic/train data/lang exp/$mic/tri1_ali exp/$mic/tri2a
  steps/align_si.sh --nj $nj --cmd "$train_cmd" \
    data/$mic/train data/lang exp/$mic/tri2a exp/$mic/tri2_ali
  # Decode,
  graph_dir=exp/$mic/tri2a/graph_${LM}
  $highmem_cmd $graph_dir/mkgraph.log \
    utils/mkgraph.sh data/lang_${LM} exp/$mic/tri2a $graph_dir
  steps/decode.sh --nj $nj --cmd "$decode_cmd" --config conf/decode.conf \
    $graph_dir data/$mic/dev exp/$mic/tri2a/decode_dev_${LM}
  steps/decode.sh --nj $nj --cmd "$decode_cmd" --config conf/decode.conf \
    $graph_dir data/$mic/eval exp/$mic/tri2a/decode_eval_${LM}
fi

if [ $stage -le 6 ]; then
  # Train tri3a, which is LDA+MLLT,
  steps/train_lda_mllt.sh --cmd "$train_cmd" \
    --splice-opts "--left-context=3 --right-context=3" \
    5000 80000 data/$mic/train data/lang exp/$mic/tri2_ali exp/$mic/tri3a
  # Align with SAT,
  steps/align_fmllr.sh --nj $nj --cmd "$train_cmd" \
    data/$mic/train data/lang exp/$mic/tri3a exp/$mic/tri3a_ali
  # Decode,
  graph_dir=exp/$mic/tri3a/graph_${LM}
  $highmem_cmd $graph_dir/mkgraph.log \
    utils/mkgraph.sh data/lang_${LM} exp/$mic/tri3a $graph_dir
  steps/decode.sh --nj $nj --cmd "$decode_cmd" --config conf/decode.conf \
    $graph_dir data/$mic/dev exp/$mic/tri3a/decode_dev_${LM} 
  steps/decode.sh --nj $nj --cmd "$decode_cmd" --config conf/decode.conf \
    $graph_dir data/$mic/eval exp/$mic/tri3a/decode_eval_${LM}
fi 

if [ $stage -le 7 ]; then
  # Train tri4a, which is LDA+MLLT+SAT
  steps/train_sat.sh  --cmd "$train_cmd" \
    5000 80000 data/$mic/train data/lang exp/$mic/tri3a_ali exp/$mic/tri4a
  # Align,
  steps/align_fmllr.sh --nj $nj --cmd "$train_cmd" \
    data/$mic/train data/lang exp/$mic/tri4a exp/$mic/tri4a_ali
  # Decode,  
  graph_dir=exp/$mic/tri4a/graph_${LM}
  $highmem_cmd $graph_dir/mkgraph.log \
    utils/mkgraph.sh data/lang_${LM} exp/$mic/tri4a $graph_dir
  steps/decode_fmllr.sh --nj $nj --cmd "$decode_cmd"  --config conf/decode.conf \
    $graph_dir data/$mic/dev exp/$mic/tri4a/decode_dev_${LM} 
  steps/decode_fmllr.sh --nj $nj --cmd "$decode_cmd" --config conf/decode.conf \
    $graph_dir data/$mic/eval exp/$mic/tri4a/decode_eval_${LM} 
fi


#exit 0 # We can skip MMI, when preparing the data to build Karel's DNN...


if [ $stage -le 8 ]; then
  # MMI training starting from the LDA+MLLT+SAT systems,
  steps/make_denlats.sh --nj $nj --cmd "$decode_cmd" --config conf/decode.conf \
    --transform-dir exp/$mic/tri4a_ali \
    data/$mic/train data/lang exp/$mic/tri4a exp/$mic/tri4a_denlats
fi

# 4 iterations of MMI seems to work well overall. The number of iterations is
# used as an explicit argument even though train_mmi.sh will use 4 iterations by
# default.
if [ $stage -le 9 ]; then
  num_mmi_iters=4
  steps/train_mmi.sh --cmd "$train_cmd" --boost 0.1 --num-iters $num_mmi_iters \
    data/$mic/train data/lang exp/$mic/tri4a_ali exp/$mic/tri4a_denlats \
    exp/$mic/tri4a_mmi_b0.1

  graph_dir=exp/$mic/tri4a/graph_${LM}
  for i in 1 2 3 4; do
    decode_dir=exp/$mic/tri4a_mmi_b0.1/decode_dev_${i}.mdl_${LM}
    steps/decode.sh --nj $nj --cmd "$decode_cmd" --config conf/decode.conf \
      --transform-dir exp/$mic/tri4a/decode_dev_${LM} --iter $i \
      $graph_dir data/$mic/dev $decode_dir 
    decode_dir=exp/$mic/tri4a_mmi_b0.1/decode_eval_${i}.mdl_${LM}
    steps/decode.sh --nj $nj --cmd "$decode_cmd"  --config conf/decode.conf \
      --transform-dir exp/$mic/tri4a/decode_eval_${LM} --iter $i \
      $graph_dir data/$mic/eval $decode_dir 
  done
fi

# DNN training. This script is based on egs/swbd/s5b/local/run_dnn.sh
# Some of them would be out of date.
if [ $stage -le 10 ]; then
  local/run_dnn.sh $mic
fi

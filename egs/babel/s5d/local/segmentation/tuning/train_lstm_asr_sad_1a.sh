#!/bin/bash

# This is a script to train a TDNN-LSTM for speech activity detection (SAD) and
# music-id using LSTM for long-context information.
# This is same as 1h, but has more layers.

set -o pipefail
set -u

. cmd.sh

# At this script level we don't support not running on GPU, as it would be painfully slow.
# If you want to run without GPU you'd have to call train_tdnn.sh with --gpu false,
# --num-threads 16 and --minibatch-size 128.

stage=0
train_stage=-10
get_egs_stage=-10
egs_opts=   

chunk_width=20

extra_left_context=60
extra_right_context=0

relu_dim=256
cell_dim=256 
projection_dim=64

# training options
num_epochs=4
initial_effective_lrate=0.0003
final_effective_lrate=0.00003
num_jobs_initial=3
num_jobs_final=8
remove_egs=false
max_param_change=0.2  # Small max-param change for small network
extra_egs_copy_cmd=   # Used if you want to do some weird stuff to egs
                      # such as removing one of the targets
dropout_schedule='0,0@0.20,0.1@0.50,0'

egs_dir=
nj=40
feat_type=raw
config_dir=

dir=
affix=1a

data_dir=exp/segmentation_1a/train_whole_hires_bp
targets_dir=exp/segmentation_1a/tri4_train_whole_combined_targets_sub3

. cmd.sh
. ./path.sh
. ./utils/parse_options.sh

if [ -z "$dir" ]; then
  dir=exp/segmentation_1a/tdnn_lstm_asr_sad
fi
dir=$dir${affix:+_$affix}

if ! cuda-compiled; then
  cat <<EOF && exit 1
This script is intended to be used with GPUs but you have not compiled Kaldi with CUDA
If you want to use GPUs (and have them), go to src/, and configure and make on a machine
where "nvcc" is installed.
EOF
fi

mkdir -p $dir

samples_per_iter=`perl -e "print int(400000 / $chunk_width)"`

if [ $stage -le 5 ]; then
  echo "$0: creating neural net configs using the xconfig parser";
  
  mkdir -p $dir/configs
  cat <<EOF > $dir/configs/network.xconfig
  input dim=`feat-to-dim scp:$data_dir/feats.scp -` name=input
  fixed-affine-layer name=lda input=Append(-2,-1,0,1,2) affine-transform-file=$dir/configs/lda.mat 

  relu-renorm-layer name=tdnn1 input=lda dim=$relu_dim add-log-stddev=true
  relu-renorm-layer name=tdnn2 input=Append(-1,0,1,2) dim=$relu_dim add-log-stddev=true
  relu-renorm-layer name=tdnn3 input=Append(-3,0,3,6) dim=$relu_dim add-log-stddev=true
  fast-lstmp-layer name=lstm1 cell-dim=$cell_dim recurrent-projection-dim=$projection_dim non-recurrent-projection-dim=$projection_dim decay-time=20 delay=-3 dropout-proportion=0.0
  relu-renorm-layer name=tdnn4 input=Append(-6,0,6,12) add-log-stddev=true dim=$relu_dim
  fast-lstmp-layer name=lstm2 cell-dim=$cell_dim recurrent-projection-dim=$projection_dim non-recurrent-projection-dim=$projection_dim decay-time=20 delay=-6 dropout-proportion=0.0
  relu-renorm-layer name=tdnn5 input=Append(-12,0,12,24) dim=$relu_dim
  relu-renorm-layer name=tdnn5-snr input=Append(tdnn4@-6,tdnn4@0,tdnn4@6,tdnn5) dim=$relu_dim

  output-layer name=output include-log-softmax=true dim=3 learning-rate-factor=0.1 input=tdnn5
EOF
  steps/nnet3/xconfig_to_configs.py --xconfig-file $dir/configs/network.xconfig \
    --config-dir $dir/configs/

  cat <<EOF >> $dir/configs/vars
add_lda=false
num_targets=3
EOF
fi

if [ $stage -le 6 ]; then
  num_utts=`cat $data_dir/utt2spk | wc -l`
  num_utts_subset=`perl -e '$n=int($ARGV[0] * 0.005); print ($n > 300 ? 300 : ($n < 12 ? 12 : $n))' $num_utts`

  steps/nnet3/train_raw_rnn.py --stage=$train_stage \
    --feat.cmvn-opts="--norm-means=false --norm-vars=false" \
    --egs.chunk-width=$chunk_width \
    --egs.dir="$egs_dir" --egs.stage=$get_egs_stage \
    --egs.chunk-left-context=$extra_left_context \
    --egs.chunk-right-context=$extra_right_context \
    --egs.use-multitask-egs=true --egs.rename-multitask-outputs=false \
    ${extra_egs_copy_cmd:+--egs.extra-copy-cmd="$extra_egs_copy_cmd"} \
    --trainer.num-epochs=$num_epochs \
    --trainer.samples-per-iter=20000 \
    --trainer.optimization.num-jobs-initial=$num_jobs_initial \
    --trainer.optimization.num-jobs-final=$num_jobs_final \
    --trainer.optimization.initial-effective-lrate=$initial_effective_lrate \
    --trainer.optimization.final-effective-lrate=$final_effective_lrate \
    --trainer.optimization.shrink-value=0.99 \
    --trainer.dropout-schedule="$dropout_schedule" \
    --trainer.rnn.num-chunk-per-minibatch=128,64 \
    --trainer.optimization.momentum=0.5 \
    --trainer.deriv-truncate-margin=10 \
    --trainer.max-param-change=$max_param_change \
    --trainer.compute-per-dim-accuracy=true \
    --cmd="$decode_cmd" --nj 40 \
    --cleanup=true \
    --cleanup.remove-egs=$remove_egs \
    --cleanup.preserve-model-interval=10 \
    --use-gpu=true \
    --use-dense-targets=false \
    --feat-dir=$data_dir \
    --targets-scp="$targets_dir/targets.scp" \
    --egs.opts="--scp2ark \"prob-to-post scp:- ark:- |\" --frame-subsampling-factor 3 --num-utts-subset $num_utts_subset" \
    --dir=$dir || exit 1

  echo 3 > $dir/frame_subsampling_factor
fi

#!/bin/bash

# Copyright 2012-2015  Johns Hopkins University (Author: Daniel Povey).
# Apache 2.0.

# This script does decoding with a neural-net.  If the neural net was built on
# top of fMLLR transforms from a conventional system, you should provide the
# --transform-dir option.

# Begin configuration section.
stage=1
transform_dir=    # dir to find fMLLR transforms.
nj=4 # number of decoding jobs.  If --transform-dir set, must match that number!
acwt=0.1  # Just a default value, used for adaptation and beam-pruning..
post_decode_acwt=1.0  # can be used in 'chain' systems to scale acoustics by 10 so the
                      # regular scoring script works.
cmd=run.pl
beam=15.0
frames_per_chunk=50
max_active=7000
min_active=200
ivector_scale=1.0
lattice_beam=8.0 # Beam we use in lattice generation.
iter=final
num_threads=1 # if >1, will use gmm-latgen-faster-parallel
scoring_opts=
skip_diagnostics=false
skip_scoring=false
extra_left_context=0
extra_right_context=0
extra_left_context_initial=-1
extra_right_context_final=-1
online_ivector_dir=
minimize=false
word_determinize=false
write_compact=true
# End configuration section.

echo "$0 $@"  # Print the command line for logging

[ -f ./path.sh ] && . ./path.sh; # source the path.
. utils/parse_options.sh || exit 1;

if [ $# -ne 3 ]; then
  echo "Usage: $0 [options] <graph-dir> <data-dir> <decode-dir>"
  echo "e.g.:   steps/nnet3/decode.sh --nj 8 \\"
  echo "--online-ivector-dir exp/nnet2_online/ivectors_test_eval92 \\"
  echo "    exp/tri4b/graph_bg data/test_eval92_hires $dir/decode_bg_eval92"
  echo "main options (for others, see top of script file)"
  echo "  --transform-dir <decoding-dir>           # directory of previous decoding"
  echo "                                           # where we can find transforms for SAT systems."
  echo "  --config <config-file>                   # config containing options"
  echo "  --nj <nj>                                # number of parallel jobs"
  echo "  --cmd <cmd>                              # Command to run in parallel with"
  echo "  --beam <beam>                            # Decoding beam; default 15.0"
  echo "  --iter <iter>                            # Iteration of model to decode; default is final."
  echo "  --scoring-opts <string>                  # options to local/score.sh"
  echo "  --num-threads <n>                        # number of threads to use, default 1."
  exit 1;
fi

graphdir=$1
data=$2
dir=$3
srcdir=`dirname $dir`; # Assume model directory one level up from decoding directory.
model=$srcdir/$iter.mdl


extra_files=
if [ ! -z "$online_ivector_dir" ]; then
  steps/nnet2/check_ivectors_compatible.sh $srcdir $online_ivector_dir || exit 1
  extra_files="$online_ivector_dir/ivector_online.scp $online_ivector_dir/ivector_period"
fi

for f in $graphdir/HCLG.fst $data/feats.scp $model $extra_files; do
  [ ! -f $f ] && echo "$0: no such file $f" && exit 1;
done

sdata=$data/split$nj;
cmvn_opts=`cat $srcdir/cmvn_opts` || exit 1;
thread_string=
[ $num_threads -gt 1 ] && thread_string="-parallel --num-threads=$num_threads"

mkdir -p $dir/log
[[ -d $sdata && $data/feats.scp -ot $sdata ]] || split_data.sh $data $nj || exit 1;
echo $nj > $dir/num_jobs


## Set up features.
echo "$0: feature type is raw"

feats="ark,s,cs:apply-cmvn $cmvn_opts --utt2spk=ark:$sdata/JOB/utt2spk scp:$sdata/JOB/cmvn.scp scp:$sdata/JOB/feats.scp ark:- |"
if [ ! -z "$transform_dir" ]; then
  echo "$0: using transforms from $transform_dir"
  [ ! -s $transform_dir/num_jobs ] && \
    echo "$0: expected $transform_dir/num_jobs to contain the number of jobs." && exit 1;
  nj_orig=$(cat $transform_dir/num_jobs)

  if [ ! -f $transform_dir/raw_trans.1 ]; then
    echo "$0: expected $transform_dir/raw_trans.1 to exist (--transform-dir option)"
    exit 1;
  fi
  if [ $nj -ne $nj_orig ]; then
    # Copy the transforms into an archive with an index.
    for n in $(seq $nj_orig); do cat $transform_dir/raw_trans.$n; done | \
       copy-feats ark:- ark,scp:$dir/raw_trans.ark,$dir/raw_trans.scp || exit 1;
    feats="$feats transform-feats --utt2spk=ark:$sdata/JOB/utt2spk scp:$dir/raw_trans.scp ark:- ark:- |"
  else
    # number of jobs matches with alignment dir.
    feats="$feats transform-feats --utt2spk=ark:$sdata/JOB/utt2spk ark:$transform_dir/raw_trans.JOB ark:- ark:- |"
  fi
elif grep 'transform-feats --utt2spk' $srcdir/log/train.1.log >&/dev/null; then
  echo "$0: **WARNING**: you seem to be using a neural net system trained with transforms,"
  echo "  but you are not providing the --transform-dir option in test time."
fi
##

if [ ! -z "$online_ivector_dir" ]; then
  ivector_period=$(cat $online_ivector_dir/ivector_period) || exit 1;
  ivector_opts="--online-ivectors=scp:$online_ivector_dir/ivector_online.scp --online-ivector-period=$ivector_period"
fi

extra_opts=
lats_wspecifier="ark:|"
if ! $write_compact; then
  extra_opts="--determinize-lattice=false"
  lats_wspecifier="ark:| lattice-determinize-phone-pruned --beam=$lattice_beam --acoustic-scale=$acwt --minimize=$minimize --word-determinize=$word_determinize --write-compact=false $model ark:- ark:- |"
fi

if [ "$post_decode_acwt" == 1.0 ]; then
  lats_wspecifier="$lats_wspecifier gzip -c >$dir/lat.JOB.gz"
else
  lats_wspecifier="$lats_wspecifier lattice-scale --acoustic-scale=$post_decode_acwt --write-compact=$write_compact ark:- ark:- | gzip -c >$dir/lat.JOB.gz"
fi

frame_subsampling_opt=
if [ -f $srcdir/frame_subsampling_factor ]; then
  # e.g. for 'chain' systems
  frame_subsampling_opt="--frame-subsampling-factor=$(cat $srcdir/frame_subsampling_factor)"
fi

if [ $stage -le 1 ]; then
  $cmd --num-threads $num_threads JOB=1:$nj $dir/log/decode.JOB.log \
    nnet3-latgen-faster$thread_string $ivector_opts $frame_subsampling_opt \
     --frames-per-chunk=$frames_per_chunk \
     --extra-left-context=$extra_left_context \
     --extra-right-context=$extra_right_context \
     --extra-left-context-initial=$extra_left_context_initial \
     --extra-right-context-final=$extra_right_context_final \
     --minimize=$minimize --word-determinize=$word_determinize \
     --max-active=$max_active --min-active=$min_active --beam=$beam \
     --lattice-beam=$lattice_beam --acoustic-scale=$acwt --allow-partial=true \
     --word-symbol-table=$graphdir/words.txt ${extra_opts} \
     "$model" \
     $graphdir/HCLG.fst "$feats" "$lats_wspecifier" || exit 1;
fi


if [ $stage -le 2 ]; then
  if ! $skip_diagnostics ; then
    [ ! -z $iter ] && iter_opt="--iter $iter"
    steps/diagnostic/analyze_lats.sh --cmd "$cmd" $iter_opt $graphdir $dir
  fi
fi


# The output of this script is the files "lat.*.gz"-- we'll rescore this at
# different acoustic scales to get the final output.
if [ $stage -le 3 ]; then
  if ! $skip_scoring ; then
    [ ! -x local/score.sh ] && \
      echo "Not scoring because local/score.sh does not exist or not executable." && exit 1;
    echo "score best paths"
    [ "$iter" != "final" ] && iter_opt="--iter $iter"
    local/score.sh $iter_opt $scoring_opts --cmd "$cmd" $data $graphdir $dir
    echo "score confidence and timing with sclite"
  fi
fi
echo "Decoding done."
exit 0;

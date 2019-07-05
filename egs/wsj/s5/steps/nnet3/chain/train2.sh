#!/bin/bash

# Copyright   2019  Johns Hopkins University (Author: Daniel Povey).  Apache 2.0.


# Begin configuration section
stage=0
cmd=run.pl
gpu_cmd_opt=
leaky_hmm_coefficient=0.1
xent_regularize=0.1
apply_deriv_weights=false   # you might want to set this to true in unsupervised training
                            # scenarios.
memory_compression_level=2  # Enables us to use larger minibatch size than we
                            # otherwise could, but may not be optimal for speed
                            # (--> set to 0 if you have plenty of memory.
dropout_schedule=
srand=0
max_param_change=1.0    # we use a smaller than normal default (it's normally
                        # 2.0), because there are two models (bottom and top).
use_gpu=yes   # can be "yes", "no", "optional", "wait"

common_opts=           # Options passed through to nnet3-chain-train and nnet3-chain-combine

num_epochs=4.0   #  Note: each epoch may actually contain multiple repetitions of
                 #  the data, for various reasons:
                 #    using the --num-repeats option in process_egs.sh
                 #    data augmentation
                 #    different data shifts (this includes 3 different shifts
                 #    of the data if frame_subsampling_factor=3 (see $dir/init/info.txt)

num_jobs_initial=1
num_jobs_final=1
initial_effective_lrate=0.001
final_effective_lrate=0.0001
groups_per_minibatch=32  # This is how you set the minibatch size.  Note: if
                         # chunks_per_group=4, this would mean 128 chunks per
                         # minibatch.

max_iters_combine=80
max_models_combine=20
diagnostic_period=5    # Get diagnostics every this-many iterations

shuffle_buffer_size=1000  # This "buffer_size" variable controls randomization of the groups
                          # on each iter.


l2_regularize=

# End configuration section



echo "$0 $@"  # Print the command line for logging

if [ -f path.sh ]; then . ./path.sh; fi
. parse_options.sh || exit 1;


if [ $# != 2 ]; then
  echo "Usage: $0  [options] <egs-dir>  <model-dir>"
  echo " e.g.: $0 exp/chain/tdnn1a_sp/egs  exp/chain/tdnn1a_sp"
  echo ""
  echo " TODO: more documentation"
  exit 1
fi

egs_dir=$1
dir=$2

set -e -u  # die on failed command or undefined variable

steps/chain/validate_randomized_egs.sh $egs_dir

for f in $dir/init/info.txt; do
  if [ ! -f $f ]; then
    echo "$0: expected file $f to exist"
    exit 1
  fi
done


frame_subsampling_factor=$(awk '/^frame_subsampling_factor/ {print $2}' <$dir/init/info.txt)
num_scp_files=$(awk '/^num_scp_files/ {print $2}' <$egs_dir/info.txt)

steps/chain/internal/get_train_schedule.py \
  --frame-subsampling-factor=$frame_subsampling_factor \
  --num-jobs-initial=$num_jobs_initial \
  --num-jobs-final=$num_jobs_final \
  --num-epochs=$num_epochs \
  --dropout-schedule="$dropout_schedule" \
  --num-scp-files=$num_scp_files \
  --frame-subsampling-factor=$frame_subsampling_factor \
  --initial-effective-lrate=$initial_effective_lrate \
  --final-effective-lrate=$final_effective_lrate \
  --schedule-out=$dir/schedule.txt


# won't work at Idiap
#if [ "$use_gpu" != "no" ]; then gpu_cmd_opt="--gpu 1"; else gpu_cmd_opt=""; fi

num_iters=$(wc -l <$dir/schedule.txt)

echo "$0: will train for $num_epochs epochs = $num_iters iterations"

# source the 1st line of schedule.txt in the shell; this sets
# lrate and dropout_opt, among other variables.
. <(head -n 1 $dir/schedule.txt)
langs=$(awk '/^langs/ { $1=""; print; }' <$dir/init/info.txt)

mkdir -p $dir/log

# Copy models with initial learning rate and dropout options from $dir/init to $dir/0
#for lang in $langs; do
  run.pl $dir/log/init_model_default.log \
      nnet3-am-copy  --learning-rate=$lrate $dropout_opt $dir/init/default.mdl $dir/0.mdl
        # nnet3-am-copy "--edits=rename-node old-name=output new-name=output-default; rename-node old-name=output-xent new-name=output-default-xent;"  - $dir/0.mdl
#done


l2_regularize_opt=""
if [ ! -z $l2_regularize ]; then
    l2_regularize_opt="--l2-regularize=$l2_regularize"
fi

x=0
if [ $stage -gt $x ]; then x=$stage; fi

while [ $x -lt $num_iters ]; do
  # Source some variables fromm schedule.txt.  The effect will be something
  # like the following:
  # iter=0; num_jobs=2; inv_num_jobs=0.5; scp_indexes=(pad 1 2); frame_shifts=(pad 1 2); dropout_opt="--edits='set-dropout-proportion name=* proportion=0.0'" lrate=0.002
  . <(grep "^iter=$x;" $dir/schedule.txt)

  echo "$0: training, iteration $x, num-jobs is $num_jobs"

  next_x=$[$x+1]
  model_in_dir=$dir/${x}.mdl
  den_fst_dir=$egs_dir/misc
  transform_dir=$dir/init
  model_out_dir=$dir/${next_x}.mdl


  # for the first 4 iterations, plus every $diagnostic_period iterations, launch
  # some diagnostic processes.  We don't do this on iteration 0, because
  # the batchnorm stats wouldn't be ready
  if [ $x -gt 0 ] && [ $[x%diagnostic_period] -eq 0 -o $x -lt 5 ]; then

    [ -f $dir/.error_diagnostic ] && rm $dir/.error_diagnostic
    for name in train heldout; do
      $cmd $gpu_cmd_opt $dir/log/diagnostic_${name}.$x.log \
         nnet3-chain-train2 --use-gpu=$use_gpu \
            --leaky-hmm-coefficient=$leaky_hmm_coefficient \
            --xent-regularize=$xent_regularize \
            $l2_regularize_opt \
            --print-interval=10  \
           $model_in_dir $den_fst_dir \
           "ark:nnet3-chain-merge-egs --minibatch-size=$groups_per_minibatch scp:$egs_dir/${name}_subset.scp ark:-|" \
           $dir/${next_x}_${name}.mdl || touch $dir/.error_diagnostic &
    done
  fi

  # if [ -d $dir/$next_x ]; then
  #   echo "$0: removing previous contents of $dir/$next_x"
  #   rm -r $dir/$next_x
  # fi
  # mkdir -p $dir/$next_x

  for j in $(seq $num_jobs); do
    scp_index=${scp_indexes[$j]}
    frame_shift=${frame_shifts[$j]}

    # not implemented yet
    $cmd $gpu_cmd_opt $dir/log/train.$x.$j.log \
         nnet3-chain-train2 --use-gpu=$use_gpu --apply-deriv-weights=$apply_deriv_weights \
         --leaky-hmm-coefficient=$leaky_hmm_coefficient --xent-regularize=$xent_regularize \
         --print-interval=10 --max-param-change=$max_param_change \
         --l2-regularize-factor=$inv_num_jobs \
         $l2_regularize_opt \
         $model_in_dir $den_fst_dir  \
         "ark:nnet3-chain-copy-egs --frame-shift=$frame_shift scp:$egs_dir/train.$scp_index.scp ark:- | nnet3-chain-shuffle-egs --buffer-size=$shuffle_buffer_size --srand=$x ark:- ark:- | nnet3-chain-merge-egs --minibatch-size=$groups_per_minibatch ark:- ark:-|" \
         $model_out_dir || touch $dir/.error &
  done
  wait
  if [ -f $dir/.error ]; then
    echo "$0: error detected training on iteration $x"
    exit 1
  fi
  [ -f $dir/$x/.error_diagnostic ] && echo "$0: error getting diagnostics on iter $x" && exit 1;
  for name in train heldout; do
      if [ -f $dir/${next_x}_${name}.mdl ]; then
          rm $dir/${next_x}_${name}.mdl
      fi
  done

  # TODO: cleanup
  x=$[x+1]
done



if [ $stage -le $num_iters ]; then
  echo "$0: doing model combination"
  nnet3-copy  --edits="rename-node old-name=output new-name=output-dummy; rename-node old-name=output-default new-name=output" \
      $dir/$num_iters.mdl $dir/final.raw
  nnet3-am-init $dir/0.mdl $dir/final.raw $dir/final.mdl
  exit 0
  if [ -d $dir/final ]; then
    echo "$0: removing previous contents of $dir/final"
    rm -r $dir/final
  fi
  mkdir -p $dir/final
  den_fst_dir=$egs_dir/misc

  [ $max_models_combine -gt $[num_iters/2] ] && max_models_combine=$[num_iters/2];
  input_model_dirs=$(for x in $(seq $[num_iters+1-max_models_combine] $num_iters); do echo $dir/$x; done)
  output_model_dir=$dir/final
  transform_dir=$dir/init

   $cmd $gpu_cmd_opt $dir/log/combine.log \
      nnet3-chain-combine --use-gpu=$use_gpu \
        --leaky-hmm-coefficient=$leaky_hmm_coefficient \
        --print-interval=10  \
        $input_model_dirs $den_fst_dir $transform_dir \
        "ark:nnet3-chain-merge-egs  scp:$egs_dir/train_subset.scp ark:-|" \
        $dir/final.mdl
fi

if [ $stage -le $[num_iters+2] ]; then
  # Accumulate some final diagnostics.  The difference with the last iteration's
  # diagnostics is that we use test-mode for the adaptation model (i.e. a target
  # model computed from all the data, not just one minibatch).
  [ -f $dir/final/.error_diagnostic ] && rm $dir/final/.error_diagnostic
  for name in train heldout; do
    den_fst_dir=$egs_dir/misc
    $cmd $gpu_cmd_opt $dir/log/diagnostic_${name}.final.log \
         nnet3-chain-train --use-gpu=$use_gpu \
         --leaky-hmm-coefficient=$leaky_hmm_coefficient \
         --xent-regularize=$xent_regularize \
         --print-interval=10  \
          $dir/final $den_fst_dir $dir/final \
           "ark:nnet3-chain-merge-egs --minibatch-size=$groups_per_minibatch scp:$egs_dir/${name}_subset.scp ark:-|" \
      || touch $dir/final/.error_diagnostic &
  done
  wait
  if [ -f $dir/final/.error_diagnostic ]; then
    echo "$0: error getting final diagnostic information"
    exit 1
  fi
  cp $dir/init/info.txt $dir/final/
fi


transform_dir=$dir/init

echo "$0: done"
exit 0

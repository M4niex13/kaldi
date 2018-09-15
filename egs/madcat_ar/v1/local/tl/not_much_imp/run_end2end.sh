#!/bin/bash
# Copyright 2017    Hossein Hadian
#           2018    Ashish Arora
set -e
stage=0
nj=30
# download_dir{1,2,3} points to the database path on the JHU grid. If you have not
# already downloaded the database you can set it to a local directory
# This corpus can be purchased here:
# https://catalog.ldc.upenn.edu/{LDC2012T15,LDC2013T09/,LDC2013T15/}
download_dir1=/export/corpora/LDC/LDC2012T15/data
download_dir2=/export/corpora/LDC/LDC2013T09/data
download_dir3=/export/corpora/LDC/LDC2013T15/data
writing_condition1=/export/corpora/LDC/LDC2012T15/docs/writing_conditions.tab
writing_condition2=/export/corpora/LDC/LDC2013T09/docs/writing_conditions.tab
writing_condition3=/export/corpora/LDC/LDC2013T15/docs/writing_conditions.tab
data_splits_dir=data/download/data_splits

. ./cmd.sh ## You'll want to change cmd.sh to something that will work on your system.
           ## This relates to the queue.
. ./path.sh
. ./utils/parse_options.sh  # e.g. this parses the above options
                            # if supplied.
./local/check_tools.sh

mkdir -p data/{train,test,dev}/data
mkdir -p data/local/{train,test,dev}
if [ $stage -le 0 ]; then

  if [ -f data/train/text ] && ! $overwrite; then
    echo "$0: Not processing, probably script have run from wrong stage"
    echo "Exiting with status 1 to avoid data corruption"
    exit 1;
  fi
  echo "$0: Downloading data splits...$(date)."
  local/download_data.sh --data_splits $data_splits_dir --download_dir1 $download_dir1 \
                         --download_dir2 $download_dir2 --download_dir3 $download_dir3

  for dataset in train dev; do
    data_split_file=$data_splits_dir/madcat.$dataset.raw.lineid
    local/extract_lines.sh --nj $nj --cmd $cmd --data_split_file $data_split_file \
        --download_dir1 $download_dir1 --download_dir2 $download_dir2 \
        --download_dir3 $download_dir3 --writing_condition1 $writing_condition1 \
        --writing_condition2 $writing_condition2 --writing_condition3 $writing_condition3 \
        --data data/local/$dataset
  done

  echo "$0: Preparing data..."
  local/prepare_data.sh --download_dir1 $download_dir1 --download_dir2 $download_dir2 \
      --download_dir3 $download_dir3 --images_scp_dir data/local \
      --data_splits_dir $data_splits_dir --writing_condition1 $writing_condition1 \
      --writing_condition2 $writing_condition2 --writing_condition3 $writing_condition3
fi

if [ $stage -le 1 ]; then
  echo "$0: Obtaining image groups. calling get_image2num_frames $(date)."
  image/get_image2num_frames.py data/train
  image/get_allowed_lengths.py --frame-subsampling-factor 4 10 data/train

  for dataset in dev train; do
    echo "$0: Extracting features and calling compute_cmvn_stats for dataset:  $dataset. $(date)"
    local/extract_features.sh --nj $nj --cmd $cmd --feat-dim 40 data/$dataset
    steps/compute_cmvn_stats.sh data/$dataset || exit 1;
  done
  echo "$0: Fixing data directory for train dataset $(date)."
  utils/fix_data_dir.sh data/train

  local/make_features.py data/test/images.scp --feat-dim 40 \
    --allowed_len_file_path data/test/allowed_lengths.txt | \
    copy-feats --compress=true --compression-method=7 \
               ark:- ark,scp:data/test/data/images.ark,data/test/feats.scp
fi

if [ $stage -le 2 ]; then
  echo "$0: Preparing BPE..."
  cut -d' ' -f2- data/train/text | local/reverse.py | \
    utils/lang/bpe/prepend_words.py --encoding 'utf-8' | \
    utils/lang/bpe/learn_bpe.py -s 700 > data/local/bpe.txt

  for set in test train dev; do
    cut -d' ' -f1 data/$set/text > data/$set/ids
    cut -d' ' -f2- data/$set/text | local/reverse.py | \
      utils/lang/bpe/prepend_words.py --encoding 'utf-8' |
      utils/lang/bpe/apply_bpe.py -c data/local/bpe.out \
      | sed 's/@@//g' > data/$set/bpe_text

    mv data/$set/text data/$set/text.old
    paste -d' ' data/$set/ids data/$set/bpe_text > data/$set/text
    rm -f data/$set/bpe_text data/$set/ids
  done

  echo "$0:Preparing dictionary and lang..."
  local/prepare_dict.sh
  utils/prepare_lang.sh --num-sil-states 4 --num-nonsil-states 8 --sil-prob 0.0 --position-dependent-phones false \
                        data/local/dict "<sil>" data/lang/temp data/lang
  utils/lang/bpe/add_final_optional_silence.sh --final-sil-prob 0.5 data/lang
fi

if [ $stage -le 3 ]; then
  echo "$0: Estimating a language model for decoding..."
  local/train_lm.sh
  utils/format_lm.sh data/lang data/local/local_lm/data/arpa/3gram_unpruned.arpa.gz \
                     data/local/dict/lexicon.txt data/lang_test
fi

if [ $stage -le 4 ]; then
  echo "$0: Calling the flat-start chain recipe... $(date)."
  local/chain/run_flatstart_cnn1a.sh --nj $nj
fi

if [ $stage -le 5 ]; then
  echo "$0: Aligning the training data using the e2e chain model...$(date)."
  steps/nnet3/align.sh --nj $nj --cmd "$cmd" \
                       --scale-opts '--transition-scale=1.0 --self-loop-scale=1.0 --acoustic-scale=1.0' \
                       data/train data/lang exp/chain/e2e_cnn_1a exp/chain/e2e_ali_train
fi

if [ $stage -le 6 ]; then
  echo "$0: Building a tree and training a regular chain model using the e2e alignments...$(date)"
  local/chain/run_cnn_e2eali_1b.sh --nj $nj
fi

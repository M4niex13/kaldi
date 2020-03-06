#!/usr/bin/env bash

# Copyright 2015  Johns Hopkins University (Author: Vijayaditya Peddinti)
#                 Guoguo Chen
# Apache 2.0
# Script to prepare the distribution from the online-nnet build

other_files= #other files to be included in the build
other_dirs=
conf_files="ivector_extractor.conf mfcc.conf online_cmvn.conf online_nnet2_decoding.conf splice.conf"
ivec_extractor_files="final.dubm final.ie final.mat global_cmvn.stats online_cmvn.conf splice_opts"

echo "$0 $@"  # Print the command line for logging
[ -f path.sh ] && . ./path.sh;
. parse_options.sh || exit 1;

if [ $# -ne 3 ]; then
   echo "Usage: $0 <lang-dir> <model-dir> <output-tgz>"
   echo "e.g.: $0 data/lang exp/nnet2_online/nnet_ms_a_online tedlium.tgz"
   exit 1;
fi

lang=$1
modeldir=$2
tgzfile=$3

for f in $lang/phones.txt $other_files; do
  [ ! -f $f ] && echo "$0: no such file $f" && exit 1;
done

build_files=
for d in $modeldir/conf $modeldir/ivector_extractor; do
  [ ! -d $d ] && echo "$0: no such directory $d" && exit 1;
done

for f in $ivec_extractor_files; do
  f=$modeldir/ivector_extractor/$f
  [ ! -f $f ] && echo "$0: no such file $f" && exit 1;
  build_files="$build_files $f"
done

# Makes a copy of the original config files, as we will change the absolute path
# to relative.
rm -rf $modeldir/conf_abs_path
mkdir -p $modeldir/conf_abs_path
cp -r $modeldir/conf/* $modeldir/conf_abs_path

for f in $conf_files; do 
  [ ! -f $modeldir/conf/$f ] && \
    echo "$0: no such file $modeldir/conf/$f" && exit 1;
  # Changes absolute path to relative path. The path entries in the config file
  # are generated by scripts and it is safe to assume that they have structure:
  # variable=path
  cat $modeldir/conf_abs_path/$f | perl -e '
    use File::Spec;
    while(<STDIN>) {
      chomp;
      @col = split("=", $_);
      if (@col == 2 && (-f $col[1])) {
        $col[1] = File::Spec->abs2rel($col[1]);
        print "$col[0]=$col[1]\n";
      } else {
        print "$_\n";
      }
    }
  ' > $modeldir/conf/$f
  build_files="$build_files $modeldir/conf/$f"
done

tar -hczvf $tgzfile $lang $build_files $other_files $other_dirs \
  $modeldir/final.mdl $modeldir/tree >/dev/null

# Changes back to absolute path.
rm -rf $modeldir/conf
mv $modeldir/conf_abs_path $modeldir/conf

#!/bin/bash                                                                        
# Copyright (c) 2016, Johns Hopkins University ( Yenda Trmal <jtrmal@gmail.com> )
# License: Apache 2.0

# Begin configuration section.  
cmd=run.pl
nj=32
acwt=0.1
beam=8
# End configuration section
. ./utils/parse_options.sh

set -e -o pipefail 
set -o nounset                              # Treat unset variables as an error

data=$1; shift;
ilang=$1; shift;
olang=$1; shift;
input=$1; shift
output=$1; shift



mkdir -p $output/log

fstreverse $olang/L.fst | fstminimize | fstreverse > $output/L.fst
$cmd JOB=1:$nj $output/log/convert.JOB.log \
  lattice-push --push-strings ark:"gunzip -c $input/lat.JOB.gz|" ark:- \| \
    lattice-align-words $ilang/phones/word_boundary.int $input/../final.mdl ark:- ark:-  \| \
    lattice-to-phone-lattice --replace-words $input/../final.mdl ark:- ark:- \| \
    lattice-align-phones $input/../final.mdl  ark:- ark:- \| \
    lattice-compose ark:- $output/L.fst ark:- \|\
    lattice-determinize-pruned --beam=$beam --acoustic-scale=$acwt ark:-  ark:"|gzip -c > $output/lat.JOB.gz"

  #lattice-1best ark:- ark:-| nbest-to-linear ark:- ark:/dev/null ark,t:- \
  #utils/int2sym.pl -f 2- $olang/words.txt | head



#!/bin/bash

# Report WER for reports and conversational 
# Copyright 2014 QCRI (author: Ahmed Ali)
# Apache 2.0

if [ $# -ne 1 ]; then
   echo "Arguments should be the gale folder, see ../run.sh for example."
   exit 1;
fi

[ -f ./path.sh ] && . ./path.sh


galeFolder=$(readlink -f $1)
symtab=./data/lang/words.txt
find exp/ -maxdepth 3 -type d -name decode\* > list_decode$$

#split the test set per type:
awk '{print $2}' $galeFolder/all.test | sort -u > $galeFolder/test_id$$

# generate the report test set
awk '{print $2}' $galeFolder/report | sort -u  > $galeFolder/report_id$$
comm -1 -2 $galeFolder/test_id$$ $galeFolder/report_id$$ > $galeFolder/report.test

# generate the conversational test set
awk '{print $2}' $galeFolder/conversational | sort -u  > $galeFolder/conversational_id$$

comm -1 -2 $galeFolder/test_id$$ $galeFolder/conversational_id$$ > $galeFolder/conversational.test

rm -fr $galeFolder/test_id$$ $galeFolder/report_id$$ $galeFolder/conversational_id$$

min_lmwt=9
max_lmwt=20
cat list_decode$$ | while read dir; do
 for type in report conversational; do
 #echo "Processing: $dir $type"
  rm -fr $dir/scoring_$type
  cp -pr $dir/scoring  $dir/scoring_$type
  ( cd $dir/scoring_$type;
    for x in *.tra test_filt.txt; do
	  sort -u $x > tmp$$
      join tmp$$ $galeFolder/${type}.test > $x
      rm -fr tmp$$
    done
   )

utils/run.pl LMWT=$min_lmwt:$max_lmwt $dir/scoring_$type/log/score.LMWT.log \
   cat $dir/scoring_${type}/LMWT.tra \| \
    utils/int2sym.pl -f 2- $symtab \| sed 's:\<UNK\>::g' \| \
    compute-wer --text --mode=present \
     ark:$dir/scoring_${type}/test_filt.txt  ark,p:- ">&" $dir/wer_${type}_LMWT
done
done


time=$(date +"%Y-%m-%d-%H-%M-%S")
echo "RESULTS generated by $USER at $time"

echo "Report Results WER:"
cat list_decode$$ | while read x; do [ -d $x ] && grep WER $x/wer_report_* | utils/best_wer.sh; done | sort -n -k2 

echo "Conversational Results WER:"
cat list_decode$$ | while read x; do [ -d $x ] && grep WER $x/wer_conversational_* | utils/best_wer.sh; done | sort -n -k2

echo "Combined Results for Reports and Conversational WER:"
cat list_decode$$ | while read x; do [ -d $x ] && grep WER $x/wer_?? $x/wer_?| utils/best_wer.sh; done | sort -n -k2

#rm list_decode$$




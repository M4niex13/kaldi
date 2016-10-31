#!/bin/bash 
data=$1
dir=$2
transcripts=$3
# tools
tok_home=/home/tools/mosesdecoder/scripts/tokenizer
lc=$tok_home/lowercase.perl
normalizer="$tok_home/normalize-punctuation.perl -l fr"
tokenizer="$tok_home/tokenizer.perl -l fr"
deescaper=$tok_home/deescape-special-chars.perl

mkdir -p $dir

# first get list of speaker directories
local/sri_gabon_conv_get_wav_dirnames.sh \
    $data \
    $dir

# get wav file names
local/sri_gabon_conv_get_wav_filenames.sh \
    $data \
    $dir

# delete the sri_gabon_conv directories un data/wavs
rm -Rf data/wavs/sri_gabon_conv_???
# copy the wav files and make their  names unique
local/sri_gabon_conv_rename_wav_filenames.pl \
    $dir/sri_gabon_conv_wav_filenames.txt \
    data/wavs

# remove the file with wav file names
rm $dir/sri_gabon_conv_wav_filenames.txt

# get the new wav file names 
local/sri_gabon_conv_get_wav_filenames.sh \
    data/wavs \
    $dir

#  get list of speaker directories again
local/sri_gabon_conv_get_wav_dirnames.sh \
    data/wavs \
    $dir

# get all speaker names
local/sri_gabon_conv_get_all_speaker_names.sh \
    data/wavs

local/get_speaker_names.pl \
    $dir/sri_gabon_conv_speaker_directory_paths.txt \
    sri_gabon_conv > \
    $dir/sri_gabon_conv_speaker_names.txt

# delete the sri_gabon_conv prompts files
rm -Rf data/prompts/sri_gabon_conv_???

# put the labels in their own directory
local/sri_gabon_conv_prompts2prompts.pl \
    $dir/sri_gabon_conv_wav_filenames.txt \
    $transcripts

# put the transcriptions in 1 file
local/get_transcriptions.sh \
    sri_gabon_conv

local/sri_gabon_get_utt2text.pl \
    $dir/sri_gabon_conv_trans_unsorted.txt > \
    $dir/sri_gabon_conv_utt2text_unsorted.txt

local/get_utt2spk.sh \
    data/wavs \
    sri_gabon_conv

local/get_spk2utt.sh \
    data/wavs \
    sri_gabon_conv

local/get_utt2wav_filename.pl \
    data/wavs \
    $dir/sri_gabon_conv_speaker_names.txt > \
    $dir/sri_gabon_conv_wav_unsorted.scp

mkdir -p data/sri_gabon_conv

sort  \
    $dir/sri_gabon_conv_wav_unsorted.scp > \
      data/sri_gabon_conv/wav.scp

sort \
    $dir/sri_gabon_conv_spk2utt_unsorted.txt > \
     data/sri_gabon_conv/spk2utt

sort \
    $dir/sri_gabon_conv_utt2spk_unsorted.txt > \
    data/sri_gabon_conv/utt2spk

sort \
     $dir/sri_gabon_conv_utt2text_unsorted.txt > \
     data/sri_gabon_conv/filename2text

sort \
    $dir/sri_gabon_conv_utt2text_unsorted.txt > \
     data/sri_gabon_conv/text

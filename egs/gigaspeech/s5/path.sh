export KALDI_ROOT=`pwd`/../../..
export PATH=$PWD/utils/:$KALDI_ROOT/tools/openfst/bin:$PWD:$PATH
[ ! -f $KALDI_ROOT/tools/config/common_path.sh ] && echo >&2 "The standard file $KALDI_ROOT/tools/config/common_path.sh is not present -> Exit!" && exit 1
. $KALDI_ROOT/tools/config/common_path.sh
export LC_ALL=C

# we use this both in the (optional) LM training and the G2P-related scripts
PYTHON='python2.7'

### Below are the paths used by the optional parts of the recipe

# SRILM is needed for LM model building
SRILM_ROOT=$KALDI_ROOT/tools/srilm
SRILM_PATH=$SRILM_ROOT/bin:$SRILM_ROOT/bin/i686-m64
export PATH=$PATH:$SRILM_PATH

# Sequitur G2P executable
sequitur=$KALDI_ROOT/tools/sequitur-g2p/g2p.py
sequitur_path="$(dirname $sequitur)/lib/$PYTHON/site-packages"

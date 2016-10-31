#!/usr/bin/perl -w
# yaounde_answers_per_utt2text.pl - get hypotheses from recognizer output
use strict;
use warnings;
use Carp;

BEGIN {
    @ARGV == 1 or croak "USAGE: yaounde_answers_per_utt2text.pl PER_UTT_FILE 
The per_utt file has 3 lines per utterence:
ref
hyp
ops
";
}

LINE: while ( my $line = <> ) {
    chomp $line;
    my ($utt,$fold,$out) = split /\s+/, $line, 3;
    my @out = split /\s+/, $out;
    if ( $fold eq "hyp" ) {
	# remove asterisks
	if ( $line =~ /\*/g ) {
	    my @del_indexes = reverse (grep { $out[$_] eq '***' } 0..$#out );
	    foreach my $item ( @del_indexes ) {
		splice (@out,$item,1);
	    }
	}
	print "$utt\t@out", "\n";
    }
}

#!/usr/bin/env perl
use strict;
use warnings;
use POSIX qw(strftime);
use benv;
use utf8;

my ($TS, $N, $I);

# Load patterns
open my $fin, "./gen-ptns.pl |" or die "Cannot run ./gen-ptns.pl script";
binmode $fin, ":utf8";
my @PTNS = <$fin>;

for my $P (@PTNS) {
	chomp $P;
	$P =~ s/\x{2022}/\%d/g;
}

my @TSTA = ();
my @NODES = ();

for ($TS=0; $TS<$BTEST_TS_LEN; $TS+=$BTEST_TS_INC) {
	my @tm = localtime($BTEST_TS_BEGIN + $TS);
	my $TS_TEXT = strftime "%FT%T.000000-05:00", @tm;
	push @TSTA, $TS_TEXT;
}

for ($N=0; $N<$BTEST_NODE_LEN; $N++) {
	my $NODE = sprintf 'node%05d', $N+$BTEST_NODE_BEGIN;
	push @NODES, $NODE;
}

my $num = 0;

my $NP;

$TS = int($BTEST_TS_BEGIN);
for my $TS_TEXT (@TSTA) {
	$NP = 0;
	for my $PTN (@PTNS) {
		$N=$BTEST_NODE_BEGIN;
		for my $NODE (@NODES) {
			printf "$TS_TEXT $NODE $PTN\n", $TS, $N
					if ($N % scalar(@PTNS) != $NP);
			$num++;
			$N++;
		}
		$NP++;
	}
	$TS += int($BTEST_TS_INC);
}

print STDERR "total messages: $num\n";
exit 0

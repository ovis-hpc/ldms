#!/usr/bin/env perl
use strict;
use warnings;
use POSIX qw(strftime);
use benv;
use Cwd 'abs_path';

my ($TS, $N, $I);
my $bpart = $ARGV[0];

my $fbase = "messages";
my $fnum = 0;

# Load patterns
open my $fin, "./gen-ptns.pl |" or die "Cannot run ./gen-ptns.pl script";
my @PTNS = <$fin>;

for my $P (@PTNS) {
	chomp $P;
	$P =~ s/\*/\%d/g;
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

my $fout;

open $fout, "> $fbase.$fnum" or die;

my $TS_TEXT_FIRST = $TSTA[0];
$TS = int($BTEST_TS_BEGIN);
$N = 0;
for my $TS_TEXT (@TSTA) {
	$NP = 0;
	for my $PTN (@PTNS) {
		for my $NODE (@NODES) {
			# printf $fout "$TS_TEXT_FIRST $NODE $PTN\n", $TS, $N
			#		if ($N % scalar(@PTNS) != $NP);
			printf $fout "$TS_TEXT $NODE $PTN\n", $TS, $N;
			$num++;
			$N++;
		}
		$NP++;
	}
	$TS += int($BTEST_TS_INC);

	# partition stuff
	if ($bpart and ($TS - $BTEST_TS_BEGIN) % (3600*6) == 0) {
		close $fout;
		$fnum++;
		open $fout, "> $fbase.$fnum" or die;
	}
}

print STDERR "total messages: $num\n";
exit 0

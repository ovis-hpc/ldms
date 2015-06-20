#!/usr/bin/env perl
use strict;
use warnings;
use POSIX qw(strftime);
use benv;

my ($TS, $N, $I);

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

for my $TS_TEXT (@TSTA) {
	$N=$BTEST_NODE_BEGIN;
	for my $NODE (@NODES) {
		print "$TS_TEXT $NODE This is a test message from ",$NODE,", num: ", $num, "\n";
		my $X = $N % 10;
		if ($X == 0) {
			print "$TS_TEXT $NODE I see fire!\n";
			$num++;
		} elsif ($X == 1) {
			print "$TS_TEXT $NODE OVIS is a sheep.\n";
			$num++;
		}
		$num++;
		$N++;
	}
}

print STDERR "total messages: $num\n";
exit 0

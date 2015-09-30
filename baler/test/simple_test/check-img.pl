#!/usr/bin/env perl
use strict;
use benv; # This will load baler environment variables into perl

my $hour_count = int( (3600 + $BTEST_TS_INC - 1) / $BTEST_TS_INC );
my $minute_count = int( (60 + $BTEST_TS_INC - 1) / $BTEST_TS_INC );

open my $fin, "./gen-ptns.pl |" or die "Cannot run ./gen-ptns.pl script";
my @PTNS = <$fin>;

# Prep comparison array
my @cmp_hour = ();
my @cmp_min = ();
my @cmp_sum = ();


my $nts = int(($BTEST_TS_LEN + $BTEST_TS_INC - 1) / $BTEST_TS_INC);

my %ptn_to_iid = {};
my $NP = 0;
for my $P (@PTNS) {
	chomp $P;
	$ptn_to_iid{$P} = $NP;
	$NP++;
}

my @iid_to_ptnid = ();
open my $bqptn, "bquery -s $BSTORE -t ptn |";

while (my $line = <$bqptn>) {
	chomp $line;
	if ($line !~ m/^ +(\d+) (.*)/) {
		next;
	}
	my $ptn_id = int($1);
	my $ptn = $2;
	my $iid = $ptn_to_iid{$ptn};
	if (! defined $iid) {
		die "Unknown pattern: $line";
	}
	$iid_to_ptnid[$iid] = $ptn_id;
}

for (my $i = 0; $i < $NP; $i++) {
	$cmp_sum[$iid_to_ptnid[$i]] = 0;
}

for (my $n = 0; $n < $BTEST_NODE_LEN; $n++) {
	my $node = $BTEST_NODE_BEGIN + $n;
	for (my $i = 0; $i < $NP; $i++) {
		if ($node % $NP == $i) {
			$cmp_hour[$iid_to_ptnid[$i]][$node] = 0;
			$cmp_min[$iid_to_ptnid[$i]][$node] = 0;
		} else {
			$cmp_hour[$iid_to_ptnid[$i]][$node] = $hour_count;
			$cmp_min[$iid_to_ptnid[$i]][$node] = $minute_count;
			$cmp_sum[$iid_to_ptnid[$i]] += $nts;
		}
	}
}

print "Checking 3600-1 image .......\n";
check_image("3600-1", @cmp_hour);
print "Checking 60-1 image .......\n";
check_image("60-1", @cmp_min);

exit 0;

sub check_image {
	my ($I, @cmp_count) = @_;
	glob $NP;
	my @sum = ();
	for (my $i = 0; $i < $NP; $i++) {
		$sum[128 + $i] = 0;
	}

	open my $BQUERY, "bquery -s $BSTORE -t img -I $I |"
						or die "bquery error";
	my ($__ptn_id, $__ts, $__comp_id) = (-1, -1, -1);

	while (my $line=<$BQUERY>) {
		chomp $line;
		my ($ptn_id, $ts, $comp_id, $count) = split /, /, $line;
		$sum[$ptn_id] += $count;
		my $expect = $cmp_count[$ptn_id][$comp_id];
		if ($count != $expect) {
			die "$ptn_id, $ts, $comp_id: Expecting '$expect', but got '$count'";
		}
		if ($comp_id <= $__comp_id && $ts == $__ts && $ptn_id == $__ptn_id) {
			die "Unexpected non-increasing component ID: $comp_id, prev_comp_id: $__comp_id";
		}
		$__comp_id = $comp_id;
		$__ptn_id = $ptn_id;
		$__ts = $ts;
	}
	for my $ptn_id (128 .. (128+$NP-1)) {
		if ($sum[$ptn_id] != $cmp_sum[$ptn_id]) {
			die "ptn_id: $ptn_id, expecting sum: $cmp_sum[$ptn_id], but got $sum[$ptn_id]";
		}
	}
}

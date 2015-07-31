#!/usr/bin/env perl
use strict;
use benv; # This will load baler environment variables into perl

my $hour_count = int( (3600 + $BTEST_TS_INC - 1) / $BTEST_TS_INC );
my $minute_count = int( (60 + $BTEST_TS_INC - 1) / $BTEST_TS_INC );

# Prep comparison array
my @cmp_hour = ();
my @cmp_min = ();
my @cmp_sum = ();


my $nts = int(($BTEST_TS_LEN + $BTEST_TS_INC - 1) / $BTEST_TS_INC);

my %ptn_to_iid = (
	"This is * test message from *, *: *" => 1,
	"* see fire!" => 2,
	"* is * sheep." => 3,
);

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

$cmp_sum[$iid_to_ptnid[1]] = $BTEST_NODE_LEN * $nts;
$cmp_sum[$iid_to_ptnid[2]] = 0;
$cmp_sum[$iid_to_ptnid[3]] = 0;

for (my $n = 0; $n < $BTEST_NODE_LEN; $n++) {
	my $node = $BTEST_NODE_BEGIN + $n;
	$cmp_hour[$iid_to_ptnid[1]][$node] = $hour_count;
	$cmp_min[$iid_to_ptnid[1]][$node] = $minute_count;
	if ($node % 10 == 0) {
		$cmp_hour[$iid_to_ptnid[2]][$node] = $hour_count;
		$cmp_min[$iid_to_ptnid[2]][$node] = $minute_count;
		$cmp_sum[$iid_to_ptnid[2]]++;
	}
	if ($node % 10 == 1) {
		$cmp_hour[$iid_to_ptnid[3]][$node] = $hour_count;
		$cmp_min[$iid_to_ptnid[3]][$node] = $minute_count;
		$cmp_sum[$iid_to_ptnid[3]]++;
	}
}

$cmp_sum[$iid_to_ptnid[2]] *= $nts;
$cmp_sum[$iid_to_ptnid[3]] *= $nts;

print "Checking 3600-1 image .......\n";
check_image("3600-1", @cmp_hour);
print "Checking 60-1 image .......\n";
check_image("60-1", @cmp_min);

exit 0;

sub check_image {
	my ($I, @cmp_count) = @_;
	my @sum = ();
	$sum[128] = 0;
	$sum[129] = 0;
	$sum[130] = 0;

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
	for my $ptn_id ((128, 129, 130)) {
		if ($sum[$ptn_id] != $cmp_sum[$ptn_id]) {
			die "ptn_id: $ptn_id, expecting sum: $cmp_sum[$ptn_id], but got $sum[$ptn_id]";
		}
	}
}

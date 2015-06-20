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

$cmp_sum[128] = $BTEST_NODE_LEN * $nts;
$cmp_sum[129] = 0;
$cmp_sum[130] = 0;

for (my $n = 0; $n < $BTEST_NODE_LEN; $n++) {
	my $node = $BTEST_NODE_BEGIN + $n;
	$cmp_hour[128][$node] = $hour_count;
	$cmp_min[128][$node] = $minute_count;
	if ($node % 10 == 0) {
		$cmp_hour[129][$node] = $hour_count;
		$cmp_min[129][$node] = $minute_count;
		$cmp_sum[129]++;
	}
	if ($node % 10 == 1) {
		$cmp_hour[130][$node] = $hour_count;
		$cmp_min[130][$node] = $minute_count;
		$cmp_sum[130]++;
	}
}

$cmp_sum[129] *= $nts;
$cmp_sum[130] *= $nts;

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

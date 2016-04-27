#!/usr/bin/env perl

use strict;
use warnings;
use benv qw(BSTORE);
use Env;



my $exp_num;

$exp_num = int($BTEST_TS_LEN / $BTEST_TS_INC) * $BTEST_NODE_LEN;
query_check($exp_num, "-P 128");

$exp_num = int($BTEST_TS_LEN / $BTEST_TS_INC);
query_check($exp_num, "-P 128 -H 0");

$exp_num = 2;
my $B=$BTEST_TS_BEGIN + $BTEST_TS_INC;
my $E=$BTEST_TS_BEGIN + $BTEST_TS_INC*2;
query_check($exp_num, "-P 128 -H 0 -B $B -E $E");

exit 0;

sub query_check {
	my ($exp_num, $cond) = @_;
	open my $fin, "bhquery $cond |" or die;
	print "Querying with condition: $cond\n";
	my $num = 0;
	while (my $line = <$fin>) {
		chomp $line;
		if ($line =~ m/^.*node(\d+).*- (\d+)$/) {
			my $x = int($1);
			my $y = int($2);
			if ($x != $y) {
				print "x: $x, y: $y\n";
				die "bad_line: $line\n";
			}
		} else {
			die "Unexpected line: $line\n";
		}
		$num++;
	}
	if ($exp_num and $exp_num != $num) {
		die "Expecting $exp_num messages, but got $num messages\n";
	}
	print "messages: $num\n";
	print "OK\n";
	print "-------\n";
	return $num;
}

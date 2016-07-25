#!/usr/bin/env perl
use strict;
use warnings;
use POSIX qw(strftime);
use benv;

for (my $N=0; $N<$BTEST_NODE_LEN; $N++) {
	my $node = sprintf 'node%05d', $N+$BTEST_NODE_BEGIN;
	my $nid = sprintf 'nid%05d', $N+$BTEST_NODE_BEGIN;
	print "$node $N\n";
	print "$nid $N\n";
}

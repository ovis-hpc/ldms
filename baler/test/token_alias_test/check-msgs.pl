#!/usr/bin/env perl
use strict;
use warnings;
use benv;

open my $f0, "./gen-log.pl | sed 's/nid/node/g' | sort |";
open my $f1, "bquery -s $BSTORE | sort |";

while (1) {
	my $l0 = <$f0>;
	my $l1 = <$f1>;
	if (!$l0 and !$l1) {
		last;
	}
	if (!$l0 or !$l1) {
		die "The number of lines not equal!";
	}
	chomp $l0;
	chomp $l1;
	if ($l0 ne $l1) {
		die "non matching messages!\n  l0: $l0\n  l1: $l1\n";
	}
}

exit 0;

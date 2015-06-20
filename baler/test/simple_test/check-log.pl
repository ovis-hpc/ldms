#!/usr/bin/env perl
use strict;
use warnings;
use benv qw(BSTORE);
use Env;

my $HELP = <<EOS
Usage: BSTORE=<STORE> ./check-log.pl
EOS
;

open my $BQUERY, "bquery -s $BSTORE |";
open my $GEN_LOG, "./gen-log.pl |";

while (!eof($GEN_LOG) && !eof($BQUERY)) {
	my $bline = <$BQUERY>;
	my $lline = <$GEN_LOG>;
	chomp $bline;
	chomp $lline;
	if (not $bline eq $lline) {
		err_exit("result mismatch");
	}
}

if (!eof($GEN_LOG)) {
	err_exit("bquery has fewer messages than input log");
}

if (!eof($BQUERY)) {
	err_exit("input log has fewer messages than bquery");
}

good_exit();

sub err_exit {
	my @param = @_;
	print "ERROR: @param\n";
	exit -1;
}

sub good_exit {
	print "good!\n";
	exit 0;
}

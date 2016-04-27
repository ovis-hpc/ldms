#!/usr/bin/env perl
use strict;
use warnings;
use benv qw(BSTORE);
use Env;

my $HELP = <<EOS
Usage: BSTORE=<STORE> ./check-log.pl
EOS
;

open my $BQUERY, "bash -c '. ./common.sh && __check_config && bhquery' |";
open my $GEN_LOG, "cat messages.* | sort |";

my %llog;
my @blog;
my $current_ts;
my $bts;
my $lts;

my $bline = <$BQUERY>;
my $lline = <$GEN_LOG>;
chomp $bline;
chomp $lline;

$bts = get_ts($bline);
$lts = get_ts($lline);

if ($bts ne $lts) {
	err_exit("Timestamp mismatch");
}

$current_ts = $bts;
$llog{$lline}++;
push @blog, $bline;

while (!eof($GEN_LOG) && !eof($BQUERY)) {
	$bline = <$BQUERY>;
	$lline = <$GEN_LOG>;
	if (!$bline || !$lline) {
		next;
	}
	chomp $bline;
	chomp $lline;
	$bts = get_ts($bline);
	$lts = get_ts($lline);

	if ($bts ne $lts) {
		err_exit("Timestamp mismatch: baler ts: '$bts', log ts: '$lts'");
	}

	if ($bts ne $current_ts) {
		# Cross-checking blog and llog
		for my $x (@blog) {
			my $tmp = $llog{$x};
			if (!$tmp) {
				err_exit("'$x' not found in original messages");
			}
			if ($tmp == 1) {
				delete $llog{$x};
			} else {
				$llog{$x}--;
			}
		}
		my @keys = keys %llog;
		my $leftover = scalar keys %llog;
		if (@keys) {
			my $msg = $keys[0];
			err_exit("'$msg' not found in baler messages");
		}

		# Initialize blog for the next round
		@blog = ();
	}

	$current_ts = $bts;
	push @blog, $bline;
	$llog{$lline}++;
}

if (!eof($GEN_LOG)) {
	err_exit("bquery has fewer messages than input log");
}

if (!eof($BQUERY)) {
	err_exit("input log has fewer messages than bquery");
}

# Cross-checking blog and llog (last chunk)
for my $x (@blog) {
	my $tmp = $llog{$x};
	if (!$tmp) {
		err_exit("'$x' not found in original messages");
	}
	if ($tmp == 1) {
		delete $llog{$x};
	} else {
		$llog{$x}--;
	}
}
my @keys = keys %llog;
my $leftover = scalar keys %llog;
if (@keys) {
	my $msg = $keys[0];
	err_exit("'$msg' not found in baler messages");
}

good_exit();

sub cross_check {
	my (%llog, @blog) = @_;
}

sub get_ts {
	my ($msg) = @_;
	if ($msg =~ m/^([^.]*)/) {
		return $1;
	}
	return "";
}

sub err_exit {
	my @param = @_;
	print "ERROR: @param\n";
	exit -1;
}

sub good_exit {
	print "good!\n";
	exit 0;
}

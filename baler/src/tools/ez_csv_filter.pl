#!/usr/bin/env perl
use strict;
use warnings;
use Getopt::Long;

my $exclude = 0;
my $include = 0;
my @filter;

GetOptions(
	"exclude" => \$exclude ,
	"include" => \$include ,
);

my $i;

if ($include == $exclude) {
	die "Please give a filtering mode (-e or -i)";
}

@filter = @ARGV;

while (my $line = <STDIN>) {
	chomp $line;
	my @d = split /, */, $line;
	if ($include) {
		print join(',', @d[@filter]);
	} else {
		delete @d[@filter];
		my @tmp;
		for my $x (@d) {
			if (defined $x) {
				push @tmp, $x;
			}
		}
		print join(',', @tmp);
	}
	print "\n";
}

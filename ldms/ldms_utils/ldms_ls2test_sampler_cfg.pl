#!/usr/bin/env perl

use strict;
use warnings;
use Socket;
use Pod::Usage;
use Getopt::Long;

my $debug = 0;
my $help;
my $gabage;
my $setname = "";
my $schema;
my $producer = "";
my $num_metrics;
my @metrics;
my $mtype;
my $vtype;
my $mname;
my $init_value;
my $raw_init_value;
my $num_elem;
my $compid = -1;
my $jobid = 0;

my $interval = -1;
my $offset = -1;
my $load;
my $add_set;

GetOptions(
	"load" => \$load,
	"add_set" => \$add_set,
	"compid=i" => \$compid,
	"jobid=i" => \$jobid,
	"producer=s" => \$producer,
	"instance=s" => \$setname,
	"interval=i" => \$interval,
	"offset=i" => \$offset,
	"debug=i" => \$debug,
	"help" => \$help
);

pod2usage(1) if $help;

my $line = <STDIN>; # first line

die "No input" if (!$line);

$line = <STDIN>; # 'METADATA ------'
chomp $line;
if (index($line, "METADATA --------") == -1) {
	print "Please make sure that the input is from ldms_ls -lv\n";
	exit;
}

$line = <STDIN>; # Get the producer name
chomp $line;
if ($producer eq "") {
	($gabage, $producer) = split / : +/, $line;
}

if ($debug) {
	print "producer: $producer\n";
}

$line = <STDIN>; # Get instance name
chomp $line;
if ($setname eq "") {
	($gabage, $setname) = split / : +/, $line;
}
if ($debug) {
	print "setname: $setname\n";
}

$line = <STDIN>; # Get schema name
chomp $line;
($gabage, $schema) = split / : +/, $line;
if ($debug) {
	print "schema: $schema\n";
}

$line = <STDIN>; # Discard meta data size

$line = <STDIN>; # Get metriccount
chomp $line;
($gabage, $num_metrics) = split / : +/, $line;
if ($debug) {
	print "metric count: $num_metrics\n";
}

# Discard the remaining description
for (my $i=1; $i <=8; $i++) {
	$line = <STDIN>;
}

my $count = 0;

while ($line = <STDIN>) {
	chomp $line;
	if (!$line) {
		last;
	}
	($mtype, $vtype, $mname, $raw_init_value) = split / +/, $line;
	$vtype =~ s/\[\]/_array/;
	if ($vtype =~ /_array/) {
		$num_elem = ($raw_init_value =~ tr/,//);
		$num_elem = $num_elem + 1;
	} else {
		$num_elem = 0;
	}
	if ($vtype =~ /_array/) {
		$init_value = (split /,/, $raw_init_value)[0];
		if ($vtype eq "char_array") {
			$init_value =~ s/\"//g;
			$num_elem = length($init_value);
		}
	} else {
		$init_value = $raw_init_value;
	}
	if ($mname eq "component_id") {
		if ($compid != -1 ) {
			$init_value = $compid;
		}
	}
	if ($mname eq "job_id") {
		if ($jobid != -1 ) {
			$init_value = $jobid;
		}
	}
	if ($debug) {
#		print "name: $mname\n";
#		print "mtype: $mtype\n";
#		print "vtype: $vtype\n";
#		print "value: $init_value\n";
#		print "$mname	$mtype	$vtype	$init_value   $num_elem\n";
	}
	push @{$metrics[$count]}, $mname;
	push @{$metrics[$count]}, $mtype;
	push @{$metrics[$count]}, $vtype;
	push @{$metrics[$count]}, $init_value;
	push @{$metrics[$count]}, $num_elem;
	$count++;
}

if ($count == 0) {
	print "Please make sure that the input is from ldms_ls -lv\n";
	exit;
}

if ($count != $num_metrics) {
	print "Number of parsed metrics vs specified metric count = $count vs
$num_metrics\n";
	exit;
}

if ($debug) {
	print "=================================\n";
}

# Construct the metric list
my $mstr = "";

foreach $count (0..@metrics-1) {
	$mname = $metrics[$count][0];
	$mtype = $metrics[$count][1];
	$vtype = $metrics[$count][2];
	$init_value = $metrics[$count][3];
	$num_elem = $metrics[$count][4];
	if ($debug) {
		print "$mtype $vtype	$mname	$init_value	$num_elem\n";
	}
	if ($count == 0) {
		$mstr = $mname.':'.$mtype.':'.$vtype.':'.$init_value.':'.$num_elem;
	} else {
		$mstr = $mstr.','.$mname.':'.$mtype.':'.$vtype.':'.$init_value.':'.$num_elem;
	}
}

# Printing configuration
if ($load) {
	print "load name=test_sampler\n";
}

print "config name=test_sampler action=add_schema schema=$schema metrics=$mstr\n";

if ($add_set) {
	print "config name=test_sampler action=add_set instance=$setname schema=$schema producer=$producer\n";
}

if ($interval >= 0) {
	print "start name=test_sampler interval=$interval";
	if ($offset >= 0) {
		print " offset=$offset\n";
	} else {
		print "\n";
	}
}

__END__

=head1 NAME

ldms_ls2test_sampler_cfg.pl

=head1 SYNOPSIS

 - Parse the ldms_ls -vl output of a **single** set
 - Generate the configuration command according to the given cmd-line attributes.
 - The created set will have the producer name and the exact metric list as the ldms_ls output.

ldms_ls2test_sampler_cfg.pl [options]


Options:
	only_schema	Print only the config line with action=add_schema

	compid		Component ID to use

	jobid		Job ID to use

	load		Print 'load name=test_sampler'

	interval	Sampling interval. If this option is given, the line
                        'start name=test_sampler interval=<interval>' will be
                        printed.

	offset          Required 'interval' to be given. Print
			'start name=test_sampler interval=<interval> offset=<offset>'

=head2 Example

ldms_ls -p 10001 -lv samplerd/meminfo | ./ldms_ls2test_sampler_cfg.pl

	"
	config name=test_sampler action=add_schema schema=meminfo metrics=component_id:M:u64:1,job_id:D:u64:0,...
	config name=test_sampler action=add_set instance=samplerd/meminfo schema=meminfo producer=samplerd
	"

ldms_ls -p 10001 -lv samplerd/meminfo | ./ldms_ls2test_sampler_cfg.pl --load

	"
	load name=test_sampler
	config name=test_sampler action=add_schema schema=meminfo metrics=component_id:M:u64:1,job_id:D:u64:0,...
	config name=test_sampler action=add_set instance=samplerd/meminfo schema=meminfo producer=samplerd
	"

ldms_ls -p 10001 -lv samplerd/meminfo | ./ldms_ls2test_sampler_cfg.pl --interval 1000000 --offset 0

	"
	config name=test_sampler action=add_schema schema=meminfo metrics=component_id:M:u64:1,job_id:D:u64:0,...
	config name=test_sampler action=add_set instance=samplerd/meminfo schema=meminfo producer=samplerd
	start name=test_sampler interval=1000000 offset=0
	"

#!/usr/bin/perl

#/* -*- c-basic-offset: 8 -*-
# * Copyright (c) 2019 National Technology & Engineering Solutions
# * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# * NTESS, the U.S. Government retains certain rights in this software.
# * Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
# *
# * This software is available to you under a choice of one of two
# * licenses.  You may choose to be licensed under the terms of the GNU
# * General Public License (GPL) Version 2, available from the file
# * COPYING in the main directory of this source tree, or the BSD-type
# * license below:
# *
# * Redistribution and use in source and binary forms, with or without
# * modification, are permitted provided that the following conditions
# * are met:
# *
# *      Redistributions of source code must retain the above copyright
# *      notice, this list of conditions and the following disclaimer.
# *
# *      Redistributions in binary form must reproduce the above
# *      copyright notice, this list of conditions and the following
# *      disclaimer in the documentation and/or other materials provided
# *      with the distribution.
# *
# *      Neither the name of Sandia nor the names of any contributors may
# *      be used to endorse or promote products derived from this software
# *      without specific prior written permission.
# *
# *      Neither the name of Open Grid Computing nor the names of any
# *      contributors may be used to endorse or promote products derived
# *      from this software without specific prior written permission.
# *
# *      Modified source versions must be plainly marked as such, and
# *      must not be misrepresented as being the original software.
# *
# *
# * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# */

use strict;
use warnings;
use Getopt::Long;
use JSON;

use constant GENERIC => qw(generic);
use constant MINIEM => qw(MINI-EM);

# well known MINIEM types
# /p=\s*(\d+) \| (STARTING): (.*) LEVEL: (\d+) COUNT: (\d+) TIMESTAMP: (.*)/)
my %MINIEMtypes = (
    p => "LDMS_V_U64",
    state => "LDMS_V_CHAR_ARRAY",
    count => "LDMS_V_U64",
    level => "LDMS_V_U64",
    timestamp => "LDMS_V_CHAR_ARRAY",
    producer => "LDMS_V_CHAR_ARRAY",
    STOREHEADER => "timestamp,producer,p,state,level,count"
    );

=head1 NAME

parser.pl


=head1 DESCRIPTION

Takes well-known output of an application and turns it into JSON to then be used with the ldms_streams interface


=head1 USAGE

Well-known formats and additional arguments:
    * --format generic --tag foo [--producer host1]
    * --format MINI-EM [--producer host1 --header 1/0]

    tag -- stream name
    producer -- optional hostname that will be added to the json
    header -- for MINI-EM only. 1 = sends header, 0 does not (default). Header for MINI-EM also has info on the



=head1 EXAMPLES

    For GENERIC:
    * Expects input format of key-value pairs like "1585345175 TAG_FOO foo_seq=0 iters=8809 t_cg=3.508063e+00 t_force=6.656258e-01 t_link=3.636193e-02"

    cat milc_volt2_hsw_20353662.out | ./parser.pl --format generic --tag TAG_FOO [--producer host1] | hello_cat_publisher -x sock -h localhost -p 16000 -a munge -s foo  -t json
    OR
    cat milc_volt2_hsw_20353662.out | ./parser.pl --format generic --tag TAG_FOO [--producer host1] > junk.out
    hello_cat_publisher -x sock -h localhost -p 16000 -a munge -s foo  -t json -f junk.out


    For MINI-EM:
    cat mini-em-timing-example.txt  | parser.pl --format MINI-EM --producer host1 --sendheader 1 | hello_cat_publisher -x sock -h localhost -p 52001 -a munge -s foo -t json

    * Header will be: '{"level":"LDMS_V_U64","p":"LDMS_V_U64","count":"LDMS_V_U64","producer":"LDMS_V_CHAR_ARRAY","state":"LDMS_V_CHAR_ARRAY","timestamp":"LDMS_V_CHAR_ARRAY","STOREHEADER":"timestamp,producer,p,state,level,count"}

    See hello_stream_store for how the stream data is stored.



=cut

my $format = 0;
my $tag = 0;
my $sendheader = 0;
undef my $producer;


sub checkargs {
    my @args = @_;
    my $format = $args[0];
    my $tag = $args[1];
    my $sendheader = $args[2];
    #producer is optional
    if (!$format){
	return 0;
    }
    if ($format eq GENERIC){
	if (!$tag){
	    return 0;
	}
    }
    if ($sendheader != 0) {
	if (!($format eq MINIEM)){
	    return 0;
	}
    }

    return 1;
}

sub MINIEMHeader {
    my $json = encode_json \%MINIEMtypes;
    print "$json\n";
}


sub MINIEMParse {
    my @args = @_;
    my $line = $args[0];

    if (($line =~ /p=\s*(\d+) \| (STARTING): (.*) LEVEL: (\d+) TIMESTAMP: (.*)/) ||
	($line =~ /p=\s*(\d+) \| (STOPPING): (.*) LEVEL: (\d+) TIMESTAMP: (.*)/)){
#	print "<$line> <$1> <$2> <$3> <$4> <$5>\n";
	my %vals;
	$vals{'p'}=$1;
	$vals{'state'}=$2;
	$vals{'id'}=$3;
	$vals{'level'}=$4;
	$vals{'timestamp'}=$5;
	if ((scalar(@args) == 2) && (defined($args[1]))){
	    $vals{'producer'}=$args[1];
	}
	my $json = encode_json \%vals;
	print "$json\n";
    } elsif (($line =~ /p=\s*(\d+) \| (STARTING): (.*) LEVEL: (\d+) COUNT: (\d+) TIMESTAMP: (.*)/) ||
	($line =~ /p=\s*(\d+) \| (STOPPING): (.*) LEVEL: (\d+) COUNT: (\d+) TIMESTAMP: (.*)/)){
	my %vals;
	$vals{'p'}=$1;
	$vals{'state'}=$2;
	$vals{'id'}=$3;
	$vals{'level'}=$4;
	$vals{'count'}=$5;
	$vals{'timestamp'}=$6;
	if ((scalar(@args) == 2) && (defined($args[1]))){
	    $vals{'producer'}=$args[1];
	}
	my $json = encode_json \%vals;
	print "$json\n";
    } else {
#	print "<$line> did not match\n";
#	die;
    }
}

sub GenericParse {
    my @args = @_;
    my $line = $args[0];
    my $tag = $args[1];
    my %vals;

    if ($line =~ /(\d+) $tag (.*)/){
#	print "<$line>\n";
	$vals{'timestamp'}=$1;
	if ((scalar(@args) == 3) && (defined($args[2]))){
	    $vals{'producer'}=$args[2];
	}
	my @arr = split/\s+/,$line;
	foreach my $arrpair (@arr){
#	    print "\t<$arrpair>\n";
	    if ($arrpair =~ /(.*)=(.*)/){
		$vals{$1}=$2;
	    }
	}
	my $json = encode_json \%vals;
	print "$json\n";
    }
}


GetOptions("format=s" => \$format,
	   "tag=s" => \$tag,
	   "producer=s" => \$producer,
	   "sendheader=i" => \$sendheader,
);

$| = 1; #flush STDOUT
checkargs($format, $tag, $sendheader) || die "Error in arguments\n";

if (($format eq MINIEM) && $sendheader){
    #send a header
    MINIEMHeader();
}
while (<>){
    chomp;
    my $line = $_;
    if ($format eq MINIEM){
	MINIEMParse($line, $producer);
    } elsif ($format eq GENERIC){
	GenericParse($line, $tag, $producer);
    } else {
	die "Unknown Format\n";
    }
}

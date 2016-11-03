#!/usr/bin/env perl

# Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
# Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
# license for use of this work by or on behalf of the U.S. Government.
# Export of this program may require a license from the United States
# Government.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the BSD-type
# license below:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#      Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#      Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditions and the following
#      disclaimer in the documentation and/or other materials provided
#      with the distribution.
#
#      Neither the name of Sandia nor the names of any contributors may
#      be used to endorse or promote products derived from this software
#      without specific prior written permission.
#
#      Neither the name of Open Grid Computing nor the names of any
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
#      Modified source versions must be plainly marked as such, and
#      must not be misrepresented as being the original software.
#
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

use strict;
use warnings;
use Socket;
use Pod::Usage;
use Getopt::Long;

my $host = "localhost";
my $port = 30003;
my $stdout;
my $help;

GetOptions(
	"host=s" => \$host,
	"port=i" => \$port,
	"stdout" => \$stdout,
	"help" => \$help
);

pod2usage(1) if $help;

my $addr = inet_aton($host);
my $paddr = sockaddr_in($port, $addr);
my $proto = getprotobyname("tcp");

unless ($stdout) {
	socket(SOCK, PF_INET, SOCK_STREAM, $proto) || die "socket: $!";
	connect(SOCK, $paddr) || die "connect: $!";
}

# Expecting metric names from the first line

my $line = <STDIN>;
chomp $line;
my @mname = split /,/, $line;

for (my $i = 2; $i < scalar @mname; $i++) {
	$mname[$i] =~ s/^\s+|\s+$//g;
}

while (my $line = <STDIN>) {
	chomp $line;
	my @values = split /,/, $line;
	my $sec = $values[0];
	my $producer = $values[2];
	my $comp_id = $values[3];
	my $job_id = $values[4];
	if ($stdout) {
		print "sec   comp_id   name   value   len\n";
	}
	for (my $i = 5; $i < scalar @values; $i++) {
		my $value = $values[$i];
		my $name = $mname[$i];
		my $len = length $name;
		if ($stdout) {
			print "$sec   $comp_id   $name   $value   $len\n";
		} else {
			my $data = pack "L>L>d>L>a*", $sec, $comp_id, $value, $len, $name;
			send SOCK, $data, 0;
		}
	}
}

unless ($stdout) {
	close(SOCK);
}
__END__

=head1 NAME

metric_csv2baler.pl - Convert metric csv input stream and send to metric
messages to baler.

=head1 SYNOPSIS

metric_csv2baler.pl [-h host] [-p port] < INPUT_CSV

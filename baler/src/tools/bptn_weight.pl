#!/usr/bin/perl

# Copyright (c) 2016 Sandia Corporation. All rights reserved.
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

use Pod::Usage;
use Getopt::Long;

# TODO: add a regex w/o something (e.g., recovery w/o application)

my $datafile = "";
my $conffile = "";
my $help;

GetOptions(
    "datafile=s" => \$datafile,
    "conffile=s" => \$conffile,
    "help" => \$help
);

pod2usage(1) if $help;

@keywords;

open ($fh, "<", $conffile) || die "Cannot open conf file <$conffile>: $!\n";

while(<$fh>){
    chomp;
    $line = $_;

    if ($line =~/^\s*$/){ #empty
	next;
    }
    if ($line =~ /^#/){ # comment
	next;
    }
    if ($line =~ /"(.*)"\s+(.*)/){
	$vals[0] = $1;
	$vals[1] = $2;
	push @keywords, [ @vals ];
    }
}
close($fh);

print "==============================\n";
print "Input Weights $conffile:\n";
print "==============================\n";
for ($i = 0; $i < scalar(@keywords); $i++){
    print "$keywords[$i][0] $keywords[$i][1]\n";
}
print "\n";

open ($fh, "<", $datafile) || die "Cannot open datafile file <$datafile>: $!\n";

# TODO: store the ptn id separately
%HoA;
$total = 0;
while(<$fh>){
    chomp;
    $line = $_;
    $total++;
    $count = 0;
    for ($i = 0; $i < scalar(@keywords); $i++){
	if ($line =~ /$keywords[$i][0]/i){
	    $count+=$keywords[$i][1];
	}
    }
    if (exists $HoA{$count}){
	push ( @{$HoA{$count}}, $line);
    } else {
	$HoA{$count}[0] = $line;
    }
}


print "==============================\n";
foreach (sort { $b <=> $a } keys(%HoA) ){
    $key = $_;
    if ($key < 1){
	next;

    }
    $num = scalar(@{$HoA{$key}});
    print "Results Weighted Matches: $key ($num/$total)\n";
}
print "==============================\n";


foreach (sort { $b <=> $a } keys(%HoA) ){
    $key = $_;
    if ($key < 1){
	next;

    }
    print "\n";
    print "==============================\n";
    $num = scalar(@{$HoA{$key}});
    print "Weighted Matches: $key ($num/$total)\n";
    print "==============================\n";
    foreach $val (@{$HoA{$key}}){
	print "(W=$key)\t$val\n";
    }
}

close($fh);

__END__

=head1 NAME

bptn_weight.pl - Use a weighted word list to reorder the output of
bquery -t PTN based on total pattern weight

=head1 SYNOPSIS

btpn_weight.pl [options]

 Options:
    -datafile The output of bquery -t PTN
    -conffile The weighted wordlist file
    -help     Show this message

The format of the conffile is quoted word followed by weight. Example:

 "bad" 1
 "critical" 1
 "do_node" 1.5

Spaces in the word within the quotes are supported.


Example output:

 ==============================
 Results Weighted Matches: 5.5 (8/26611)
 Results Weighted Matches: 5 (10/26611)
 Results Weighted Matches: 4.5 (3/26611)
 Results Weighted Matches: 4 (22/26611)
 ==============================

 ==============================
 Weighted Matches: 5.5 (8/26611)
 ==============================
 (W=5.5)     3678                  318 2015-03-03T07:52:59.000000-07:00 2016-05-18T10:44:08.000000-06:00 controllermessages • • - - do_node_quiesce: ORB timeout quiesce while already • for resiliency; saving orb_quiesce_mask as •
 (W=5.5)     4077                    2 2015-02-12T13:35:41.000000-07:00 2015-02-12T13:35:41.000000-07:00 controllermessages • • - - do_node_•: failed to read quiesce MMR for node • (error -•); not setting flags
 (W=5.5)     4237                    2 2015-04-24T07:29:58.000000-06:00 2015-04-24T07:30:02.000000-06:00 controllermessages • • - - do_node_halt: write_quiesce_mmr for node • failed; • -•
 (W=5.5)     4330                    2 2015-02-27T12:54:31.000000-07:00 2015-02-27T12:54:43.000000-07:00 controllermessages • • - - do_node_quiesce: node • failed to write quiesce in_progress flag at location • (error -•)






#!/usr/bin/perl

use strict;
use warnings;
use Getopt::Long;
use Pod::Usage;
use IO::Handle;

$| = 1;

my $headerfile;
my $datafile;
my $oprefix;
my $help;

my $Time_m = "#Time";
my $PName_m = "ProducerName";
my $cid_m = "component_id";
my $jid_m = "job_id";
my $aries_m = "aries_rtr_id";

my $TimeCol = -1;
my $PNameCol = -1;
my $cidCol = -1;
my $jidCol = -1;
my $ariesCol = -1;

my $req_flits_idx = 0;
my $req_stalls_idx = 1;
my $rsp_flits_idx = 2;
my $rsp_stalls_idx = 3;

my $uncert_a = 0.8;

my %NICHoA; # could be more than 1 NIC in a line
my %LastHoA; #store last line by aries.

GetOptions(
    "header=s" => \$headerfile,
    "data=s" => \$datafile,
    "oprefix=s" => \$oprefix,
    "help" => \$help
) or die ("CLI argument error");

if ($help) {
    pod2usage( -verbose => 2);
}

die "--header=<HEADERFILE> is needed" if not $headerfile;
die "--data=<DATAFILE> is needed" if not $datafile;
die "--oprefix=<OUTPUT PREFIX> is needed" if not $oprefix;

open my $headerin, "<$headerfile" or
    die "Cannot open $headerfile";

open my $datain, "<$datafile" or
    die "Cannot open $datafile";

parse_header($headerin);
close $headerin;

print_indicies($headerin);

parse_data($datain,$oprefix);

close $datain;

exit 0;

sub print_dataheader {

    my ($fh) = @_;

    print $fh "#CurrTime,PrevTime,DT,ProducerName,component_id,job_id,aries_rtr_id,NIC";
    print $fh ",req_flits,req_stalls,rsp_flits,rsp_stalls";
    print $fh ",req_stalls_due_to_rsp,req_stall_flits_ratio,req_stall_flits_ratio_flag";
    print $fh ",node_bp,node_bp_flag,hsn_rsp_bp,hsn_rsp_bp_flag\n";
    return;
}

sub parse_data {

    my ($dataconf,$oprefix) = @_;
#    my $linecount = 0;
    while (my $line = <$dataconf>){
	chomp $line;

	if ($line =~/^#/){
#	    print "Skipping <$line>\n";
	    next;
	}

	my @curr = split(/,/,$line);
	my $curr_aries =  $curr[$ariesCol];
	my $curr_cid = $curr[$cidCol];

#	print "\n\nParsing <$line> SEARCHING for key <$curr_aries>\n\n";

	if (exists $LastHoA{$curr_aries}){
#	    print "Have a prev line for $curr_aries\n";

	    my $dt = $curr[$TimeCol] - $LastHoA{$curr_aries}[$TimeCol];

	    foreach my $nic (sort keys %NICHoA){
		my $idx = $NICHoA{$nic}[$req_flits_idx];
		my $curr_req_flits = $curr[$idx];
		my $prev_req_flits = $LastHoA{$curr_aries}[$idx];
#		print "<$curr_req_flits> <$prev_req_flits>\n";
		my $req_flits = $curr_req_flits - $prev_req_flits;

		$idx = $NICHoA{$nic}[$req_stalls_idx];
		my $curr_req_stalls = $curr[$idx];
		my $prev_req_stalls = $LastHoA{$curr_aries}[$idx];
#		print "<$curr_req_stalls> <$prev_req_stalls>\n";
		my $req_stalls = $curr_req_stalls - $prev_req_stalls;

		$idx = $NICHoA{$nic}[$rsp_flits_idx];
		my $curr_rsp_flits = $curr[$idx];
		my $prev_rsp_flits = $LastHoA{$curr_aries}[$idx];
#		print "<$curr_rsp_flits> <$prev_rsp_flits>\n";
		my $rsp_flits = $curr_rsp_flits - $prev_rsp_flits;

		$idx = $NICHoA{$nic}[$rsp_stalls_idx];
		my $curr_rsp_stalls = $curr[$idx];
		my $prev_rsp_stalls = $LastHoA{$curr_aries}[$idx];
#		print "<$curr_rsp_stalls> <$prev_rsp_stalls>\n";
		my $rsp_stalls = $curr_rsp_stalls - $prev_rsp_stalls;

		my $req_stalls_due_to_rsp;
		if ($req_flits > ($rsp_flits + $rsp_stalls)){
		    $req_stalls_due_to_rsp = 0;
		} else {
		    $req_stalls_due_to_rsp = $rsp_flits + $rsp_stalls - $req_flits;
		}

		my $ratio = 0;
		my $ratio_flag = 0;
		if ($req_flits == 0){
		    $ratio_flag = 1;
		} else {
		    $ratio = $req_stalls/$req_flits;
		}

		my $node_bp = 0;
		my $node_bp_flag = 0;
# THE NEXT LINE IS THE ORIGINAL LINE FROM DUNCAN
#		if ($req_stalls > ($uncert_a * $req_stalls_due_to_rsp)){
# THE NEXT LINE IS THE REPLACEMENT LINE FROM EDWIN
		if ($req_stalls_due_to_rsp > ($uncert_a * $req_stalls)){
		    $node_bp = 0;
		} else {
		    if ($req_flits == 0){
			$node_bp = 0;
			$node_bp_flag = 1;
		    } else {
			$node_bp = $ratio;
		    }
		}

		my $hsn_rsp_bp;
		my $hsn_rsp_bp_flag = 0;
		if ($rsp_flits == 0){
		    $hsn_rsp_bp = 0;
		    $hsn_rsp_bp_flag = 1;
		} else {
		    $hsn_rsp_bp = $rsp_stalls/$rsp_flits;
		}

		my $ofile = $oprefix . "." . $curr_aries . "." . $nic . "." . $curr_cid . ".bp";
		open(my $ofileh, '>>', $ofile) or die "Cannot open '$ofile'$!";

		print $ofileh  "$curr[$TimeCol],$LastHoA{$curr_aries}[$TimeCol],$dt";
		print $ofileh ",$curr[$PNameCol],$curr[$cidCol],$curr[$jidCol],$curr[$ariesCol],$nic";
		print $ofileh ",$req_flits,$req_stalls,$rsp_flits,$rsp_stalls";
		print $ofileh ",$req_stalls_due_to_rsp,$ratio,$ratio_flag";
		print $ofileh ",$node_bp,$node_bp_flag,$hsn_rsp_bp,$hsn_rsp_bp_flag\n";

		close $ofileh;
	    }
	} else {
#	    print "DONT Have a prev line for $curr_aries\n";
	    foreach my $nic (sort keys %NICHoA){
		my $ofile = $oprefix . "." . $curr_aries . "." . $nic . "." . $curr_cid . ".bp";
		open(my $ofileh, '>', $ofile) or die "Cannot open '$ofile'$!";
		print_dataheader($ofileh);
		close $ofileh;
	    }
	}

	$LastHoA{$curr_aries} = [ @curr ];
#	$linecount++;
    }

    return;
}

sub parse_header {
    my ($headerconf) = @_;

    my $line = <$headerconf>;
    chomp $line;

#    print "<$line>\n";
    my @vals = split(/,/,$line);
    my $count = 0;
    foreach my $val (@vals){
	if ($val =~ /$Time_m/){
	    $TimeCol = $count;
	} elsif ($val =~ /$PName_m/){
	    $PNameCol = $count;
	} elsif ($val =~ /$cid_m/){
	    $cidCol = $count;
	} elsif ($val =~ /$cid_m/){
	    $cidCol = $count;
	} elsif ($val =~ /$jid_m/){
	    $jidCol = $count;
	} elsif ($val =~ /$aries_m/){
	    $ariesCol = $count;
	} elsif ($val =~ /AR_NL_PRF_REQ_PTILES_TO_NIC_(\d)_FLITS/){
	    $NICHoA{$1}[$req_flits_idx] = $count;
	} elsif ($val =~ /AR_NL_PRF_REQ_PTILES_TO_NIC_(\d)_STALLED/){
	    $NICHoA{$1}[$req_stalls_idx] = $count;
	} elsif ($val =~ /AR_NL_PRF_RSP_NIC_(\d)_TO_PTILES_FLITS/){
	    $NICHoA{$1}[$rsp_flits_idx] = $count;
	} elsif ($val =~ /AR_NL_PRF_RSP_NIC_(\d)_TO_PTILES_STALLED/){
	    $NICHoA{$1}[$rsp_stalls_idx] = $count;
	}
	$count++;
    }

    return;
}

sub print_indicies{

    print "IN print_indicies\n";

    print "\n\n";
    print "Indicies:\n";
    print "TimeCol = $TimeCol\n";
    print "PNameCol = $PNameCol\n";
    print "cidCol = $cidCol\n";
    print "jidCol = $jidCol\n";
    print "ariesCol = $ariesCol\n";

    foreach my $key (keys %NICHoA){
	print "$key\n";
	foreach my $i ( 0 .. $#{ $NICHoA{$key} } ) {
	    print " $i = $NICHoA{$key}[$i]";
	}
	print "\n";
    }
    print "\n\n";

    return;
}

__END__

=head1 NAME

backpressure.pl - Generate output file of backpressure metrics

=head1 SYNOPSIS

backpressure.pl [OPTIONS]

Example: ./backpressure.pl --header=<path to HEADERFILE> --data=<path to DATAFILE> --oprefix=<OUTPUT PREFIX>

=head1 OPTIONS

=over 4

=item B<--header> Path to HEADERFILE

=item B<--data> Path to DATAFILE

=item B<--oprefix> Prefix of any OUTPUT FILE. Naming Convention oprefix.<aries_rtr_id>.<nic>.<component_id>

=back

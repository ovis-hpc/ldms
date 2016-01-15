#!/usr/bin/env perl
# Please see the embedded document after __END__ section at the end of the file.
# Or, run ./gen-config.pl -h
use strict;
use warnings;
use Getopt::Long;
use Pod::Usage;
use File::Path;

my $grpconfpath;
my $cmdconfpath;
my $outdir = "out";
my $help;
my $update_interval = 1000000;
my $connect_interval = 20000000;

GetOptions(
"groupconf=s" => \$grpconfpath,
"cmdconf=s" => \$cmdconfpath,
"outdir=s" => \$outdir,
"update-interval=i" => \$update_interval,
"connect-interval=i" => \$connect_interval,
"help" => \$help
) or die ("CLI argument error");

if ($help) {
	pod2usage( -verbose => 2);
}

die "--groupconf=<GROUP_CONF_FILE> is needed" if not $grpconfpath;
die "--cmdconf=<CMD_CONF_FILE> is needed" if not $cmdconfpath;

open my $grpconf, "<$grpconfpath" or
	die "Cannot open $grpconfpath";

open my $cmdconf, "<$cmdconfpath" or
	die "Cannot open $cmdconfpath";

my %group;
my %outfile;
my %add_list;
my %watch_entry;
my %watched_by_entry;
my %add_args;
my %agg_list;

while (my $line = <$grpconf>) {
	chomp $line;
	my ($grp, $ent) = split / +/, $line;
	if (!$group{$grp}) {
		$group{$grp} = [];
	}
	push @{$group{$grp}}, $ent;
}

close $grpconf;

# key check debug
if (0) { # disable debug
	foreach my $k (keys %group) {
		print "--- $k ---\n";
		my @v = @{$group{$k}};
		foreach my $x (@v) {
			print "  $x\n";
		}
	}
}

while (my $line = <$cmdconf>) {
	chomp $line;
	my ($tag1, $verb, $tag2, @args) = split /[ |]+/, $line;
	if ($verb eq "add") {
		if (not $add_list{$tag1}) {
			$add_list{$tag1} = [];
		}
		push @{$add_list{$tag1}}, $tag2;
		if (@args) {
			$add_args{"$tag1+$tag2"} = \@args;
		}
		$agg_list{$tag1} = 1;
	} elsif ($verb eq "watch") {
		$watch_entry{$tag1} = $tag2;
		$watched_by_entry{$tag2} = $tag1;
		$agg_list{$tag1} = 1;
	} else {
		die "Error: unknown verb '$verb'";
	}
}

close $cmdconf;

for my $agg (keys %agg_list) {
	process_agg($agg);
}

foreach my $k (keys %outfile) {
	close $outfile{$k};
}

exit 0;

sub parse_node_entry {
	my ($node) = @_;
	my @x = split /:/, $node;
	return @x;
}

sub get_out_conf_path {
	my ($tag, $node) = @_;
	my ($host, $xprt, $port) = parse_node_entry($node);
	return "$outdir/$tag.$host.$port";
}

sub expand_match_grp {
	my ($fout, $updtr, $grp) = @_;
	my $ref = $group{$grp};
	if (not $ref) {
		die "Group expansion error: $grp not found.";
	}
	my @entries = @{$ref};
	if (not @entries) {
		die "Group expansion error: $grp is empty.";
	}
	foreach my $ent (@entries) {
		my ($host, $xprt, $port) = parse_node_entry($ent);
		print $fout <<EOS
updtr_match_add name=$updtr regex=$host/.* match=inst
EOS
	}
}

sub process_agg {
	my ($agg) = @_;
	process_producers($agg);
	process_updaters($agg);
}

# get all possible producers for $1
sub process_producers {
	my ($agg) = @_;
	my @grps;
	my @prdcrs;

	my $fout = $outfile{$agg};
	if (not $fout) {
		my $path = get_out_conf_path($agg, $group{$agg}[0]);
		open $fout, ">$path"
			or die "Cannot open file: $path";
		$outfile{$agg} = $fout;
	}

	my $ref = $add_list{$agg};
	if ($ref) {
		push @grps, @{$ref};
	}

	my $w = $watch_entry{$agg};
	if ($w) {
		$ref = $add_list{$w};
		if ($ref) {
			push @grps, @{$ref};
		}
	}

	my %uniq;

	for my $grp (@grps) {
		$ref = $group{$grp};
		die "Group eror: group '$grp' not found" if ! $ref;
		my @entries = @{$ref};
		for my $ent (@entries) {
			my ($host, $xprt, $port) = parse_node_entry($ent);
			my $prdcr = "$grp.$host.$port";
			if ($uniq{$prdcr}) {
				next;
			}
			print $fout <<EOS
prdcr_add name=$prdcr host=$host port=$port xprt=$xprt interval=$connect_interval
prdcr_start name=$prdcr
EOS
;
			$uniq{$prdcr} = 1;
		}
	}
}

sub process_updaters {
	my ($agg) = @_;
	my @active_grps;
	my @failover_grps;

	my $fout = $outfile{$agg};
	if (not $fout) {
		my $path = get_out_conf_path($agg, $group{$agg}[0]);
		open $fout, ">$path"
			or die "Cannot open file: $path";
		$outfile{$agg} = $fout;
	}

	my $ref = $add_list{$agg};
	if ($ref) {
		push @active_grps, @{$ref};
	}

	my $w = $watch_entry{$agg};
	if ($w) {
		$ref = $add_list{$w};
		if ($ref) {
			push @failover_grps, @{$ref};
		}
	}

	for my $grp (@active_grps) {
		my $updtr = "$grp";
		my $prdcr = "$grp\..*";
		add_updater($agg, $agg, $updtr, $prdcr, $grp);
	}

	for my $grp (@failover_grps) {
		my $updtr = "${w}_failover.$grp";
		my $prdcr = "$grp\..*";
		add_updater($agg, $w, $updtr, $prdcr, $grp);
	}
}

sub add_updater {
	my ($agg, $as, $updtr, $prdcr, @grps) = @_;

	my $fout = $outfile{$agg};
	if (not $fout) {
		my $path = get_out_conf_path($agg, $group{$agg}[0]);
		open $fout, ">$path"
			or die "Cannot open file: $path";
		$outfile{$agg} = $fout;
	}

	print $fout <<EOS
updtr_add name=$updtr interval=$update_interval offset=100000
EOS
;

	print $fout <<EOS
updtr_prdcr_add name=$updtr regex=$prdcr
EOS
;
	for my $grp (@grps) {
		my @leaves;
		my $argref = $add_args{"$as+$grp"};
		if ($argref) {
			@leaves = @{$argref};
		} else {
			@leaves = get_leaves($grp);
		}
		foreach my $ent (@leaves) {
			expand_match_grp($fout, $updtr, $ent);
		}
	}
}

# Get the leaves of the add tree started from $1
sub get_leaves {
	my ($node) = @_;
	my $list_ref = $add_list{$node};
	if (not $list_ref) {
		# This is a leaf
		return $node;
	}
	my @list = @{$list_ref};
	my @ret_list;
	foreach my $ent (@list) {
		my $rlist_ref = $add_args{"$node+$ent"};
		if ($rlist_ref) {
			push @ret_list, @{$rlist_ref};
		} else {
			my @tmp = get_leaves($ent);
			if (@tmp) {
				push @ret_list, @tmp;
			}
		}
	}
	return @ret_list;
}

__END__

=head1 NAME

gen-config.pl - Generate LDMS config with failover

=head1 SYNOPSIS

gen-config.pl [OPTIONS]

Example: ./gen-config.pl -cmd cmd -g grp

=head1 OPTIONS

=over 4

=item B<--groupconf> GROUP_CONFIG_FILE

Group configuration file (see GROUP CONIG FILE FORMAT section below)

=item B<--cmdconf> CMD_CONFIG_FILE

Command config file (see COMMAND CONFIG FILE FORMAT below).

=item  B<--update-interval> MICRO_SEC

Set update interval.

=item  B<--connect-interval> MICRO_SEC

Set connect interval.

=back

=head1 GROUP CONFIG FILE FORMAT

A configuration file contains lines of the following format:

 <TAG> <HOSTNAME>:<XPRT>:<PORT>

The <XPRT> and <PORT> is the listening transport/port of the ldmsd entity.

=head2 Example

 grp1 nid00001:ugni:10001
 grp1 nid00002:ugni:10001
 grp2 nid00003:ugni:10001
 grp2 nid00004:ugni:10001
 grp3 nid00005:ugni:10001
 grp3 nid00006:ugni:10001
 grp4 nid00007:ugni:10001
 grp4 nid00008:ugni:10001
 grp5 nid00009:ugni:10001
 grp5 nid00010:ugni:10001
 grp6 nid00011:ugni:10001
 grp6 nid00012:ugni:10001
 grp7 nid00013:ugni:10001
 grp7 nid00014:ugni:10001
 grp8 nid00015:ugni:10001
 grp8 nid00016:ugni:10001
 grp9 nid00017:ugni:10001
 grp9 nid00018:ugni:10001
 grpA nid00019:ugni:10001
 grpA nid00020:ugni:10001
 grpB nid00021:ugni:10001
 grpB nid00022:ugni:10001
 grpC nid00023:ugni:10001
 grpC nid00024:ugni:10001
 agg11 nid10001:ugni:10001
 agg12 nid10002:ugni:10001
 agg13 nid10003:ugni:10001
 agg14 nid10004:ugni:10001
 agg21 svc1:sock:10001
 agg22 svc2:sock:10001
 ovs1 miner1:sock:10001
 ovs2 miner2:sock:10001
 ovs3 miner3:sock:10001

=head1 COMMAND CONFIG FILE FORMAT

Each line in the file is in the following format:

 <subject> add <object> [ | <object restriction> ]
 <subject> watch <object>

<subject> is meant to be an aggregator, where <object> can be a group name of
sample ldmsd or an aggregator.

For add command, if the group restriction is given, the <subject> aggregator
will only add the groups specified (see example below).

For this version, an ldmsd shall only watch another ldmsd. An ldmsd should only
be watched by another ldmsd.

=head2 Example

 agg11 add grp1
 agg11 add grp2
 agg11 add grp3
 agg12 add grp4
 agg12 add grp5
 agg12 add grp6
 agg13 add grp7
 agg13 add grp8
 agg13 add grp9
 agg14 add grpA
 agg14 add grpB
 agg14 add grpC
 agg11 watch agg12
 agg12 watch agg13
 agg13 watch agg14
 agg14 watch agg11
 agg21 add agg11
 agg21 add agg12
 agg22 add agg13
 agg22 add agg14
 agg21 watch agg22
 agg22 watch agg21
 ovs1 add agg21 | grp1 grp4
 ovs1 add agg22 | grp7 grpA
 ovs2 add agg21 | grp2 grp5
 ovs2 add agg22 | grp8 grpB
 ovs3 add agg21 | grp3 grp6
 ovs3 add agg22 | grp9 grpC
 ovs1 watch ovs2
 ovs2 watch ovs3
 ovs3 watch ovs1

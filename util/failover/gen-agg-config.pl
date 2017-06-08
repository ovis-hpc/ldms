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
my $update_offset = $update_interval/10;
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
my %group_noregex;
my %outfile;
my %failoverfile;
my %recoverfile;
my %add_list;
my %added_by_list;
my %watch_entry;
my %watched_by_entry;
my %add_args;
my %add_cfg;
my %agg_list;
my %failover_list;

while (my $line = <$grpconf>) {
	chomp $line;
	my ($grp, $ent, $first) = split / +/, $line;
	if (!$group{$grp}) {
		$group{$grp} = [];
	}
	push @{$group{$grp}}, $ent;
	if ($first && ($first eq "first")) {
		$group_noregex{$grp} = 1;
	}
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
	my @cfg;
	chomp $line;
	$line =~ m/([^|-]+) *(\| *([^-]+))?(- *(.*))?/;
	my ($tag1, $verb, $tag2) = split / +/, $1;
	if ($verb eq "add") {
		if (not $add_list{$tag1}) {
			$add_list{$tag1} = [];
		}
		push @{$add_list{$tag1}}, $tag2;
		if (not $added_by_list{$tag2}) {
			$added_by_list{$tag2} = [];
		}
		push @{$added_by_list{$tag2}}, $tag1;
		if ($3) {
			my @args = split / +/, $3;
			$add_args{"$tag1+$tag2"} = \@args;
		}
		if ($5) {
			@cfg = split / +/, $5;
			$add_cfg{"$tag1+$tag2"} = \@cfg;
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

sub get_out_conf_file {
	my ($tag, $node) = @_;
	my ($host, $xprt, $port) = parse_node_entry($node);
	return "$tag.$host.$port";
}

sub get_out_conf_path {
	my ($tag, $node) = @_;
	my $fname = get_out_conf_file($tag, $node);
	return "$outdir/$fname";
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

sub get_grp_hosts {
	my ($grp) = @_;
	my $ref = $group{$grp};
	my @hosts;
	if (not $ref) {
		die "Group expansion error: $grp not found.";
	}
	my @entries = @{$ref};
	if (not @entries) {
		die "Group expansion error: $grp is empty.";
	}
	foreach my $ent (@entries) {
		my ($host, $xprt, $port) = parse_node_entry($ent);
		push @hosts, $host
	}
	return @hosts;
}

sub hosts_to_regexp {
	my @hosts = @_;
	my $regexp = "\\(" . (join "\\|", @hosts) . "\\)";
	return $regexp;
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

	my %uniq;
	my $ref = $add_list{$agg};
	if ($ref) {
		add_prdcr($fout, \%uniq, $agg, @{$ref});

		# Taking care of failover of the added aggregators,
		# not the failover of the same level.
		for my $ent (@{$ref}) {
			if ($agg_list{$ent}) {
				# it is an aggregator.
				my $ent_watcher = $watched_by_entry{$ent};
				if ($ent_watcher) {
					add_prdcr($fout, \%uniq, $agg, ($ent_watcher));
				}
			}
		}
	}

	my $w = $watch_entry{$agg};
	if ($w) {
		$ref = $add_list{$w};
		if ($ref) {
			add_prdcr($fout, \%uniq, $w, @{$ref});
		}
	}
}

sub add_prdcr {
	my ($fout, $uniq_ref, $as, @grps) = @_;
	for my $grp (@grps) {
		my $int;
		my $cfgref = $add_cfg{"$as+$grp"};
		if ($cfgref) {
			$int = @{$cfgref}[0]
		} else {
			$int = $connect_interval;
		}
		my $ref = $group{$grp};
		die "Group eror: group '$grp' not found" if ! $ref;
		my @entries = @{$ref};
		for my $ent (@entries) {
			my ($host, $xprt, $port) = parse_node_entry($ent);
			my $prdcr = "$grp.$host.$port";
			if ($uniq_ref->{$prdcr}) {
				next;
			}
			print $fout <<EOS
prdcr_add name=$prdcr host=$host port=$port xprt=$xprt type=active interval=$int
prdcr_start name=$prdcr
EOS
;
			$uniq_ref->{$prdcr} = 1;
		}
	}
}

sub process_updaters {
	my ($agg) = @_;
	my @active_grps;
	my @failover_grps;
	my $path = get_out_conf_path($agg, $group{$agg}[0]);

	my $fout = $outfile{$agg};
	if (not $fout) {
		open $fout, ">$path"
			or die "Cannot open file: $path";
		$outfile{$agg} = $fout;
	}

	my $ref = $add_list{$agg};
	if ($ref) {
		#active groups
		push @active_grps, @{$ref};
		for my $grp (@active_grps) {
			my $updtr = "$grp";
			my $prdcr = "$grp\\..*";
			add_updater($agg, $agg, $updtr, $prdcr, $grp);
			if ($agg_list{$grp}) {
				# If $grp is an aggregator, take care of the failover-recover conf.
				failover_recover($grp, $agg, $updtr, "stop");

				my $grp_watcher = $watch_entry{$grp};
				if ($grp_watcher) {
					$updtr = "${grp}_failover.$grp_watcher";
					$prdcr = "$grp_watcher\\..*";
					add_updater($agg, $agg, $updtr, $prdcr, $grp);
					failover_recover($grp, $agg, $updtr, "start");
				}
			}
		}
	}

	my $w = $watch_entry{$agg};
	if ($w) {
		$ref = $add_list{$w};
		if ($ref) {
			for my $grp (@{$ref}) {
				my $updtr = "${w}_failover.$grp";
				my $prdcr = "$grp\\..*";
				add_updater($agg, $w, $updtr, $prdcr, $grp);
				failover_recover($w, $agg, $updtr, "start");
			}
		}
	}

	for my $grp (@active_grps) {
		my $updtr = "$grp";
		start_updater($agg, $updtr);
	}
}
sub failover_recover {
	my ($failed_agg, $watcher, $updtr, $action) = @_;

	my $path = get_out_conf_path($failed_agg, $group{$failed_agg}[0]);
	my $suffix = get_out_conf_file($watcher, $group{$watcher}[0]);

	my $failover_fout = $failoverfile{$failed_agg}{$watcher};
	if (not $failover_fout) {
		my $failover_path = "$path.failover.$suffix";
		open $failover_fout, ">$failover_path"
			or die "Cannot open file: $failover_path";
		$failoverfile{$failed_agg}{$watcher} = $failover_fout;
	}

	my $recover_fout = $recoverfile{$failed_agg}{$watcher};
	if (not $recover_fout) {
		my $recover_path = "$path.recover.$suffix";
		open $recover_fout, ">$recover_path"
			or die "Cannot open file: $recover_path";
		$recoverfile{$failed_agg}{$watcher} = $recover_fout;
	}

	if ($action eq "stop") {
		print $failover_fout <<EOS
updtr_stop name=$updtr
EOS
;
		print $recover_fout <<EOS
updtr_start name=$updtr
EOS
;
	} elsif ($action eq "start") {
		print $failover_fout <<EOS
updtr_start name=$updtr
EOS
;
		print $recover_fout <<EOS
updtr_stop name=$updtr
EOS
;
	}
}

sub add_updater {
	my ($agg, $as, $updtr, $prdcr, $grp) = @_;
	my $d1;
	my $int;
	my $offset;
	my $cfgref = $add_cfg{"$as+$grp"};
	if ($cfgref) {
		($d1, $int, $offset) = @{$cfgref};
	} else {
		$int = $update_interval;
		$offset = $update_offset;
	}
	my $fout = $outfile{$agg};
	if (not $fout) {
		my $path = get_out_conf_path($agg, $group{$agg}[0]);
		open $fout, ">$path"
			or die "Cannot open file: $path";
		$outfile{$agg} = $fout;
	}

	print $fout <<EOS
updtr_add name=$updtr interval=$int offset=$offset
EOS
;

	print $fout <<EOS
updtr_prdcr_add name=$updtr regex=$prdcr
EOS
;
	my @leaves;
	my $argref = $add_args{"$as+$grp"};
	if ($argref) {
		@leaves = @{$argref};
	} else {
		@leaves = get_leaves($grp);
	}
	if ($group_noregex{$agg}) {
		return;
	}

	# otherwise, process regex
	my @hosts;

	foreach my $ent (@leaves) {
		my @h = get_grp_hosts($ent);
		push @hosts, @h;
	}
	my $regexp = hosts_to_regexp(@hosts);
	print $fout <<EOS
updtr_match_add name=$updtr regex=$regexp/.* match=inst
EOS
;
}

sub start_updater {
	my ($agg, $updtr) = @_;

	my $fout = $outfile{$agg};
	if (not $fout) {
		my $path = get_out_conf_path($agg, $group{$agg}[0]);
		open $fout, ">$path"
			or die "Cannot open file: $path";
		$outfile{$agg} = $fout;
	}

	print $fout <<EOS
updtr_start name=$updtr
EOS
;
}

sub append_failover_script {
	my ($path, $agg, $updtr) = @_;

	my $fout = $failoverfile{$agg};
	if (not $fout) {
		my $failover_path = "$path-failover";
		open $fout, ">$failover_path"
			or die "Cannot open file: $failover_path";
		$failoverfile{$agg} = $fout;
	}

	print $fout <<EOS
updtr_start name=$updtr
EOS
;

	$fout = $recoverfile{$agg};
	if (not $fout) {
		my $recover_path = "$path-recover";
		open $fout, ">$recover_path"
			or die "Cannot open file: $recover_path";
		$recoverfile{$agg} = $fout;
	}

	print $fout <<EOS
updtr_stop name=$updtr
EOS
;
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

gen-agg-config.pl - Generate LDMS config with failover

=head1 SYNOPSIS

gen-agg-config.pl [OPTIONS]

Example: ./gen-agg-config.pl --groupconf <path to GROUP_CONFIG_FILE> --cmdconf
<path to CMD_CONFIG_FILE> --outdir <output directory>

=head1 OPTIONS

=over 4

=item B<--groupconf> Path to GROUP_CONFIG_FILE

Group configuration file (see GROUP CONIG FILE FORMAT section below)

=item B<--cmdconf> Path to CMD_CONFIG_FILE

Command config file (see COMMAND CONFIG FILE FORMAT below).

=item B<--outdir> Path to the output directory

=back

=head1 DESCRIPTION

C<gen-agg-config.pl> generates configuration files for aggregators. There are
two mandatory input files: Group Definition file and Command file. The Group
Definition file contains the definitions of ldmsd groups in the system (see in
section GROUP CONFIG FILE). The Command file defines actions between two groups
(see in section COMMAND CONFIG FILE).

The script determines which group is an aggregator when the group I<add> another
group in the Command file.

=head2 ASSUMPTIONS AND LIMITATIONS

=over

=item

Each aggregator has at most one Watcher. A Watcher of aggregator A is an
aggregator that, when aggregator A dies, will collect the sets usually
collected by aggregator A.

=item

In the case that aggregator A and its aggregator, called AA, die. The sets
collected by aggregator A will not be picked up by any aggregators. The sets
collected by aggregator AA will be picked up by the AA's Watcher.

=back

=head1 RESULTS

There are three types of configuration files generated for each aggregator:
initial, failover, and recovery configuration files.

=over 2

=item - B<initial configuration file>

The configuration file given to ldmsd when it is started.

The file name format is C<$group_name.$host.$listen_port>, e.g.
agg11.nid10001.411.

=item - B<failover configuration file>

Configuration files given to the Watcher and its aggregator when it dies.

The file name format is
C<$group_name.$host.$port.failover.$Watcher.$Watcher_host.$Watch_port> or
C<$group_name.$host.$port.failover.$Aggregator.$Aggregator_host.$Aggregator_port>,
e.g., agg11.nid10001.411.failover.agg12.nid10002.411 or
agg11.nid10001.411.failover.agg21.nid20001.411, respectively.

I<agg11.nid10001.411.failover.agg12.nid10002.411> must be given to the aggregator
running on nid10002 listening for data on port 411 via the C<ldmsd_controller>
program.

=item - B<recovery configuration file>

Configuration files given to the Watcher and its aggregator when it is revived.

The file name format is similar to that of the failover configuration files,
except that the word 'failover' is replaced by the word 'recover.'

=back

=head1 GROUP CONFIG FILE

A configuration file contains lines of the following format:

 <TAG> <HOSTNAME>:<XPRT>:<PORT>

The <XPRT> and <PORT> is the listening transport/port of the ldmsd entity.

A tag could coresspond to multiple ldmsd entities. In this case such ldmsd
entities are in the same group and can be represented by the tag name.

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

=head1 COMMAND CONFIG FILE

Each line in the file defines an action between two groups. There are two
actions: I<add> and I<watch>.

The format is

  <subject> <action> <object> [ action arguments ]

<subject> is meant to be an aggregator, where <object> can be a group name of
sample ldmsd or an aggregator.

  <subject> add <object> [ | group_restriction ] [ - re-connect_interval update_interval update_offset ]

If the group restriction is given, the <subject> aggregator
will only add the specified groups. From the example below, C<ovs1 add agg22 |
grp7 grpA> means that I<ovs1> will collect from I<agg22> only the sets belonging
to I<grp7> and I<grpA>.

C<agg11 add grp1 - 20000000 1000000 100000> means that I<agg11> will reconnect
to the ldmsd entities in I<grp1> at 20-second interval if such entity is
disconnected from I<agg11> and update the sets from the ldmsd entities in
I<grp1> every 1 second with the 100 millisecond offset.

 <subject> watch <object>

For this version, an ldmsd shall only watch another ldmsd. An ldmsd should only
be watched by another ldmsd.

=head2 Example

 agg11 add grp1 - 20000000 1000000 100000
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
 ovs1 add agg21 | grp1 grp4 - 20000000 1000000 300000
 ovs1 add agg22 | grp7 grpA
 ovs2 add agg21 | grp2 grp5
 ovs2 add agg22 | grp8 grpB
 ovs3 add agg21 | grp3 grp6
 ovs3 add agg22 | grp9 grpC
 ovs1 watch ovs2
 ovs2 watch ovs3
 ovs3 watch ovs1

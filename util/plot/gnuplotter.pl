#!/usr/bin/perl
use FileHandle;
use Pod::Usage;
use Getopt::Long;
use IO::Handle;

# run with --help (or any other invalid flag to get usage

# EXAMPLE DATA:
#aries_ROOT_/projects/ovis/ClusterData/chama/store_csv_roll/node # head chama_meminfo.HEADER.1420873200
##Time, Time_usec, CompId, DirectMap1G, DirectMap2M, DirectMap4k, Hugepagesize, HugePages_Surp, HugePages_Rsvd, HugePages_Free, HugePages_Total, AnonHugePages, HardwareCorrupted, VmallocChunk, VmallocUsed, VmallocTotal, Committed_AS, CommitLimit, WritebackTmp, Bounce, NFS_Unstable, PageTables, KernelStack, SUnreclaim, SReclaimable, Slab, Shmem, Mapped, AnonPages, Writeback, Dirty, SwapFree, SwapTotal, Mlocked, Unevictable, Inactive(file), Active(file), Inactive(anon), Active(anon), Inactive, Active, SwapCached, Cached, Buffers, MemFree, MemTotal
#
#aries_ROOT_/projects/ovis/ClusterData/chama/store_csv_roll/node # head chama_meminfo.1420873200
#1420873220.007950, 7950, 11006, 65011712, 2060288, 4096, 2048, 0, 0, 0, 0, 800768, 0, 34325234252, 433536, 34359738367, 1306540, 33026828, 0, 0, 0, 4176, 6520, 104400, 864728, 969128, 15488, 13696, 843000, 0, 0, 0, 0, 0, 0, 8304340, 1335624, 17508, 840980, 8321848, 2176604, 0, 9158516, 496932, 53594412, 66053656
#1420873220.010210, 10210, 12006, 65011712, 2060288, 4096, 2048, 0, 0, 0, 0, 104448, 0, 34324665400, 1034180, 34359738367, 1184468, 64145748, 0, 0, 0, 20440, 8264, 474488, 1123596, 1598084, 67580, 71828, 286780, 0, 36, 31163384, 31249992, 128, 65536, 922664, 1054100, 192940, 97064, 1115604, 1151164, 1440, 2031368, 12984, 60555952, 65791516

# EXAMPLE COMMAND:
#./gnuplotter.pl --header "./chama_meminfo.HEADER.1420873200" --file "./chama_meminfo.1420873200" --var Active --ofile "Active.png" --titles 0" --tempdir=./testdir --compids 1,3-5,10


$usetitles=1;
$fname ='';
$hname = '';
$oname = '';
$tempdir = '';
$var = '';
$beg = '';
$end = '';
my $help;

%compidList;
$compid_title = "CompId";
$compidcol = -1;
$timecol = 1; # 1 in gnuplot 0 in perl

sub SplitCompIds{
    #need better error checking

    my $list = $_[0];

    @ranges=split(/,/, $list);
    foreach $range (@ranges){
	@endpts = split(/-/, $range);
	if ((scalar(@endpts) < 1) || (scalar(@endpts > 2))){
	    die "Problem with compids <$list>\n";
	}
	if (scalar(@endpts) == 1){
	    $compidList{$endpts[0]} = '';
	} else {
	    for ($i = $endpts[0]; $i <= $endpts[1]; $i++){
		$compidList{$i} = '';
	    }
	}
    }
}


GetOptions ('titles=i' => \$usetitles,
	    'file=s' => \$fname,
	    'ofile=s' => \$oname,
	    'header=s' => \$hname,
	    'var=s' => \$var,
	    'tempdir=s' => \$tempdir,
	    'compids=s' => \$compids,
	    'beg=i' => \$beg,
	    'end=i' => \$end,
	    "help" =>\$help
    ) || pod2usage(1);

pod2usage(1) if $help;


if ($oname eq ''){
    die  "Missing ofile\n";
}

if ($hname eq ''){
    die  "Missing hname\n";
}

if ($compids eq ''){
    die "Missing compids\n";
}

if ($var eq ''){
    die  "Missing var\n";
}

if ($tempdir eq ''){
    die  "Missing tempdir\n";
}


printf("file=<$fname> header=<$hname> var=<$var> begTime=<$beg> endTime=<$end> useTitles=<$usetitles> tempdir=<$tempdir> ofile=<$oname> compids=<$compids>\n");

SplitCompIds($compids);

$count = 0;
foreach $id (sort(keys %compidList)){
    $compidList{$id} = $count++;
}



#read the first line of the header
open($fp, "<", $hname) || die "Cannot open <$hname>\n";
while (<$fp>){
#    printf("<$_>\n");
    chomp;
    $line = $_;
    if ($line  =~ /#/){
	@vals = split(/,/, $line);
	$count = 0;
	foreach $val (@vals){
	    # NOTE: we always use single compid. This will have to be changed if there is variable or multiple.
	    if ($val =~ /CompId/){
		$compidcol = $count;
	    }
	    if ($val =~ /$var/){
		$varcol = $count;
	    }
	    $count++;
	}
    }
}

close($fp);
($compidcol != -1) || die "Cannot find CompId in header\n";
($varcol != -1) || die "Cannot find <$var> in header\n";


@outfiles;
if ($fname eq ''){
    # we have the files, but build their names
    foreach $id (sort(keys %compidList)){
	push(@outfiles, "$tempdir/$var.$id.out");
    }
} else {
    # split into the output files
    @fhout;

    foreach $id (sort(keys %compidList)){
	$outfile = "$tempdir/$var.$id.out";
	$j = FileHandle->new();
	open($j, ">", $outfile) || die "Cannot open <$outfile]>\n";
	push(@outfiles, $outfile);
	push(@fhout, $j);
    }


#write the temp output files
    open ($fh, "<", $fname) || die "Cannot open <$fname>\n";
    while (<$fh>){
	chomp;
	$line = $_;
	@vals = split(/,/, $line);
	$compid = int($vals[$compidcol]);
	if (exists $compidList{$compid}){
	    $idx = $compidList{$compid};
#	    printf("Printing <$line> to <$compid> idx <$idx>\n");
	    print {$fhout[$idx]} "$line\n";
	}
    }
    close($fh);

    foreach $j (@fhout){
	close($j);
    }
}


# now increment compid and var col for gnuplot
$compidcol++;
$varcol++;

open my $GP, '|-', 'gnuplot';

say {$GP} 'set term png enhanced font \'/usr/share/fonts/liberation/LiberationSans-Regular.ttf\' 40 size 3600,2400';
#say {$GP} 'set terminal png size 2400,1200';
say {$GP} "set output '$oname'";
say {$GP} "set format x '%0.f'";
say {$GP} "set xtics rotate by -90";
say {$GP} "set xlabel 'Time(epoch)";
say {$GP} "set ylabel '$var'";
say {$GP} "set datafile separator ','";
if (!($beg  eq '') || !($end  eq '')){
    say {$GP} "set xrange [$beg:$end]";
}


printf("Plotting $var using $timecol:(\$$varcol)\n");


$firsttime = 1;
$cmd = '';
$ltitle='';
foreach $id (sort(keys %compidList)){
    $lfname = $outfiles[$compidList{$id}];
    $filesize = -s $lfname;
    printf("filesize <$lfname> <$filesize>\n");
    if (int($filesize) > 0){
	if ($usetitles){
	    $ltitle = "CompId $id";
	}
	if ($firsttime){
	    $cmd = "plot '$lfname' using $timecol:(\$$varcol) title '$ltitle' with lines lw 2";
	} else {
	    $cmd .= ", '$lfname' using $timecol:(\$$varcol) title '$ltitle' with lines lw 2";
	}
	$firsttime = 0;
    }
}

say {$GP} "$cmd";

close $GP;

__END__

=head1 NAME

gnuplotter.pl - Plot csv output data with gnuplot.

=head1 SYNOPSIS

gnuplotter.pl [options]

 Options:
    --file       Datafile.
    --tempdir    Directory of temporary per-compid intermediate files. User has to delete these.
    --var        Variable to plot (from HEADER).
    --header     Header file. 1st line has variable names.
    --compids    CompIds to plot (e.g., 1,3-5,7,9).
    --titles     Print titles (1) or not (0). Optional.
    --beg        Time to begin. Optional.
    --end        Time to end. Optional.
    --ofile      Output png file.
 If no file but with tempdir, it will plot the data from tempdir, instead of writing to it.

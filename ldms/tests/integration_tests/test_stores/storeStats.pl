#!/usr/bin/perl

# will print out:
# * instances per comp id
# * instances of Flag != 0 per comp id (store_derived_csv only)
#
# store_csv header: #Time, Time_usec, CompId, DirectMap1G, DirectMap2M
# store_csv data: 1410282240.006516, 6516, 11006, 65011712, 2060288
#
# store_derived_csv header: Time, Time_usec, DT, DT_usec, CompTime, Time_usec, DT, DT_usec, CompId, MemFree (x 1.00e+00), Active (x 1.00e+00), Flag
# store_derived data: 1410282260.006855, 6855, 20.000028, 28, 11002, 57953220, 1201320, 0

#use strict;
#use warnings;
use Getopt::Long;

my $mintime = 0;
my $maxtime = 0;
my $compidcol = -1;
my $flagcol = -1;
my $timecol = -1;
my $storefile = "";
my $help = '';


my %entrieshash = ();
my %flaghash = ();

my $result = GetOptions (
    'version' => sub {
	print "storeStats.pl version 1.0\n"; exit;},
    'help' => $help,
    'b=i' => \$mintime,
    'e=i' => \$maxtime,
    's=s' => \$storefile,
    'c=i' => \$compidcol,
    'f=i' => \$flagcol,
    't=i' => \$timecol,
    );


if ($help){
    print "storeStats.pl\n",
    print "\t-b Mintime epoch (optional) \n",
    print "\t-e Maxtime epoch (optional) \n",
    print "\t-s Store file\n",
    print "\t-f Flag col\n",
    print "\t-c CompId col\n",
    print "\t-t Time col\n",
    print "\t--help Displays this message\n";
    exit;
}


if ($storefile eq ""){
    die "no storefile";
}


if ($compidcol == -1){
    die "no compidcol";
}


if ($timecol == -1){
    die "no timecol";
}



open(IN, $storefile) || die "Cannot open storefile";
while (<IN>){
    chomp;
    my $line = $_;

    #skip any line that starts with a # or a blank line
    if ($line eq ""){
	next;
    }

    if ($line[0] eq '#'){
	next;
    }

    @vals = split(/,/,$line);

    if ($timecol >= scalar(@vals)){
	next;
    }

    if ($compidcol >= scalar(@vals)){
	next;
    }


    if ($flagcol >= scalar(@vals)){
	die "Invalid flag col";
    }


    if (($mintime != 0) && ($vals[$timecol] < $mintime)){
#	printf("skipping min <$line> %f > %d \n", $vals[$timecol], $mintime);
	next;
    }


    if (($maxtime != 0) && ($vals[$timecol] > $maxtime)){
#	printf("skipping max <$line> %f < %d \n", $vals[$timecol], $maxtime);
	next;
    }


    if (exists $entrieshash{$vals[$compidcol]}){
	$entrieshash{$vals[$compidcol]}++;
    } else {
	$entrieshash{$vals[$compidcol]} = 1;
    }

    if ($flagcol >= 0){
	printf("<$line> Checking %d flaghash\n", $vals[$flagcol]);
	if ($vals[$flagcol] > 0){
	    printf("Setting flaghash\n");
	    if (exists $hash{$vals[$compidcol]}){
		$flaghash{$vals[$compidcol]}++;
	    } else {
		$flaghash{$vals[$compidcol]} = 1;
	    }
	}
    }
}

printf("Entries:\n");	
# Print num entries for each one, sorted on val (not compid)
foreach $cid (sort { $entrieshash{$a} <=> $entrieshash{$b} } keys %entrieshash) {
    printf("%d %d\n", $cid, $entrieshash{$cid});

}

# Print non-zero flags for each one, sorted on val (not compid)

printf("Non-zero flags:\n");	
foreach my $key (sort { $flaghash{$a} <=> $flaghash{$b} } keys %flaghash) {
    printf("%d %d\n", $key, $flaghash{$key});
}

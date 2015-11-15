#!/usr/bin/perl

# cat XXX.HEADER | ./header.pl <metric_name>
# awk -F "," '{print $1 $6}' /home/XXX.dat

$cmp = $ARGV[0];
#print "<$cmp>\n";
while(<STDIN>){
    chomp;
    $line = $_;
    @vals = split(/, /, $line);
    for ($i = 0; $i < scalar(@vals); $i++){
#	print "comp <$vals[$i]>\n";
	if ($vals[$i] =~ /$cmp/){
	    $j = $i+1;  # increment before because awk commands will start with col 1
	    print "$vals[$i] $j\n";
	}
    }
}

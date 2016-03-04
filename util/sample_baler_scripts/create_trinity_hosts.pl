#!/usr/bin/perl

#while(<>){
#    chomp;
#    $line = $_;
#    @vals = split(/,/, $line);
#    for ($i = 0; $i < scalar(@vals); $i++){
#	if ($vals[$i]){
#	    print "$i <$vals[$i]>\n";
#	} else {
#	    print "$i no val\n";
#	}
#    }
#
#}


#c = 0 - 11
#x = 0 - 4 (last one to move)
#c = 0 - 2
#s = 0 - 15
#n = 0 - 3


$count = 0;
# like c0-0c2s15n0
for ($x = 0; $x < 5; $x++){
    for ($c = 0; $c < 12; $c++){
	if (($c > 5) && ($x == 4)){
	    next;
	}

	for ($cc = 0; $cc < 3; $cc++){
	    for ($s = 0; $s < 16; $s++){
		for ($n = 0; $n < 4; $n++){
#		    print $count++ . " c" . $c. "-" . $x . "c" . $cc . "s" . $s . "n" . $n . "\n";
		    print "c" . $c. "-" . $x . "c" . $cc . "s" . $s . "n" . $n . "\n";
		}
	    }
	}
    }
}

# not prepping for the rest of the hosts since we don't know how the renumbering will work

# like c0-0c2s15
for ($x = 0; $x < 5; $x++){
    for ($c = 0; $c < 12; $c++){
	if (($c > 5) && ($x == 4)){
	    next;
	}

	for ($cc = 0; $cc < 3; $cc++){
	    for ($s = 0; $s < 16; $s++){
#		print $count++ . " c" . $c. "-" . $x . "c" . $cc . "s" . $s . "\n";
		print "c" . $c. "-" . $x . "c" . $cc . "s" . $s . "\n";
	    }
	}
    }
}

# like c0-0
for ($x = 0; $x < 5; $x++){
    for ($c = 0; $c < 12; $c++){
	if (($c > 5) && ($x == 4)){
	    next;
	}
#	print $count++ . " c" . $c. "-" . $x . "\n";
	print "c" . $c. "-" . $x . "\n";
    }
}

# like c0-0c0s0a0
for ($x = 0; $x < 5; $x++){
    for ($c = 0; $c < 12; $c++){
	if (($c > 5) && ($x == 4)){
	    next;
	}

	for ($cc = 0; $cc < 3; $cc++){
	    for ($s = 0; $s < 16; $s++){
#		print $count++ . " c" . $c. "-" . $x . "c" . $cc . "s" . $s . "a0" . "\n";
		print  "c" . $c. "-" . $x . "c" . $cc . "s" . $s . "a0" . "\n";
	    }
	}
    }
}


#special (smw, mom, etc)...if we run some lines, we will see what these are...

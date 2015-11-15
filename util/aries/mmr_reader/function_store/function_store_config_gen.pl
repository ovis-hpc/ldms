#!/usr/bin/perl

# nodes 0->3
# rows 0->4
# cols 0->7
# vc 0->7

################################
# RTR (See also PTILES below)
################################

for ($node = 0; $node < 4; $node++){
    print "##\n";
    print "## NODE " . $node . "\n";
    print "##\n";

    $schema = "rtr_mmr_". $node;

    print "$schema aries_rtr_id RAWTERM 1 aries_rtr_id 1 1\n";
    print "#\n";

    for ($row = 0; $row < 5; $row++){
	for ($col = $node*2; $col <= $node*2+1; $col++){
	    $header = "AR_RTR_". $row . "_" . "$col";

	    # SUM VC's (PAGE 32)
	    print "$schema $header" . "_INQ_PRF_INCOMING_FLIT_VC_SUM SUM_N 8 ";
	    for ($vc = 0; $vc < 8; $vc++){
		print $header . "_INQ_PRF_INCOMING_FLIT_VC" . $vc;
		if ($vc != 7){
		    print ",";
		}
	    }
	    print " 1 1\n";
	    print "#\n";

	    # SUM VC's (PAGE 33)
	    print "$schema $header" . "_INQ_PRF_INCOMING_PKT_VC_SUM_FILTER_FLIT_SUM_CNT SUM_N 8 ";
	    for ($vc = 0; $vc < 8; $vc++){
		print $header . "_INQ_PRF_INCOMING_PKT_VC" . $vc . "_FILTER_FLIT" . $vc ."_CNT";
		if ($vc != 7){
		    print ",";
		}
	    }
	    print " 1 1\n";
	    print "#\n";

	    # RATIO OF INCREASE IN STALLS TO FLITS (PAGE 33)
	    print "$schema $header" . "_INQ_PRF_ROWBUS_STALL_CNT_DELTA DELTA 1 " .
		$header . "_INQ_PRF_ROWBUS_STALL_CNT";
	    print " 1 0\n";
	    print "$schema $header" . "_INQ_PRF_INCOMING_FLIT_VC_SUM_DELTA DELTA 1 " .
		$header . "_INQ_PRF_INCOMING_FLIT_VC_SUM";
	    print " 1 0\n";
	    print "$schema $header" . "_STALL_FLIT_RATIO DIV_AB 2 " .
		$header . "_INQ_PRF_ROWBUS_STALL_CNT_DELTA," .
		$header . "_INQ_PRF_INCOMING_FLIT_VC_SUM_DELTA";
	    print " 1 1\n";
	    print "##\n";
	} #col
    } #row
} #node


##################################################################
# NIC
# Pay particular attention to flits (plural) vs flit (singular)
##################################################################
print "##\n";
print "## NIC\n";
print "##\n";
$schema = "nic_mmr";
# PAGE 12
@NIC_ARGS = ("AR_NIC_RSPMON_PARB_EVENT_CNTR_AMO","AR_NIC_RSPMON_PARB_EVENT_CNTR_WC",
	     "AR_NIC_RSPMON_PARB_EVENT_CNTR_BTE_RD", "AR_NIC_RSPMON_PARB_EVENT_CNTR_IOMMU");

print "$schema aries_rtr_id RAWTERM 1 aries_rtr_id 1 1\n";
print "#\n";

#RATIOS OF INCREASE IN FLITS TO INCREASE IN PKTS
for ($num = 0; $num < scalar(@NIC_ARGS); $num++){
    $quan = $NIC_ARGS[$num];

    print "$schema $quan" . "_FLITS_DELTA DELTA 1 $quan" . "_FLITS";
    print " 1 0\n";
    print "$schema $quan" . "_PKTS_DELTA DELTA 1 $quan" . "_PKTS";
    print " 1 0\n";
    print "$schema $quan". "_FLITS_PKTS_RATIO DIV_AB 2 ".
	$quan . "_FLITS_DELTA," .
	$quan . "_PKTS_DELTA";
    print " 1 1\n";
    print "##\n";
}

$NIC_STALL_FLITS_SCALE = 100;
# PAGES 9 AND 12
@NIC_STALL_ARGS = ("AR_NIC_NETMON_ORB_EVENT_CNTR_REQ", "AR_NIC_RSPMON_PARB_EVENT_CNTR_PI");
#RATIOS OF INCREASE IN FLITS TO INCREASE IN PKTS
#RATIOS OF INCREASE IN STALLS TO FLITS
for ($num = 0; $num < scalar(@NIC_STALL_ARGS); $num++){
    $quan = $NIC_STALL_ARGS[$num];

    print "$schema $quan" . "_FLITS_DELTA DELTA 1 $quan" . "_FLITS";
    print " 1 0\n";
    print "$schema $quan" . "_PKTS_DELTA DELTA 1 $quan" . "_PKTS";
    print " 1 0\n";
    print "$schema $quan" . "_STALLED_DELTA DELTA 1 $quan" . "_STALLED";
    print " 1 0\n";
    print "$schema $quan". "_FLITS_PKTS_RATIO DIV_AB 2 ".
	$quan . "_FLITS_DELTA," .
	$quan . "_PKTS_DELTA";
    print " 1 1\n";
    print "$schema $quan". "_STALL_FLITS_RATIO_" . "$NIC_STALL_FLITS_SCALE DIV_AB 2 ".
	$quan . "_STALLED_DELTA," .
	$quan . "_FLITS_DELTA";
    print " $NIC_STALL_FLITS_SCALE 1\n";
    print "##\n";
}


################################
# PTILES
################################
#RATIOS OF INCREASE IN FLITS TO INCREASE IN PKTS
#RATIOS OF INCREASE IN STALLS TO INCREASE IN FLITS
print "##\n";
print "## PTILES\n";
print "##\n";

# PAGE 18
$nquan = "AR_NL_PRF_REQ_PTILES_TO_NIC";
for ($num = 0; $num < 4; $num++){
    $schema = "rtr_mmr_". $num;
    $quan = $nquan . "_" . $num;
    print "$schema $quan" . "_FLITS_DELTA DELTA 1 $quan" . "_FLITS";
    print " 1 0\n";
    print "$schema $quan" . "_PKTS_DELTA DELTA 1 $quan" . "_PKTS";
    print " 1 0\n";
    print "$schema $quan" . "_STALLED_DELTA DELTA 1 $quan" . "_STALLED";
    print " 1 0\n";
    print "$schema $quan". "_FLITS_PKTS_RATIO DIV_AB 2 ".
	$quan . "_FLITS_DELTA," .
	$quan . "_PKTS_DELTA";
    print " 1 1\n";
    print "$schema $quan". "_STALL_FLITS_RATIO DIV_AB 2 ".
	$quan . "_STALLED_DELTA," .
	$quan . "_FLITS_DELTA";
    print " 1 1\n";
    print "##\n";
}

# PAGE 18
$nquan = "AR_NL_PRF_RSP_PTILES_TO_NIC";
for ($num = 0; $num < 4; $num++){
    $schema = "rtr_mmr_". $num;
    $quan = $nquan . "_" . $num;
    print "$schema $quan" . "_FLITS_DELTA DELTA 1 $quan" . "_FLITS";
    print " 1 0\n";
    print "$schema $quan" . "_PKTS_DELTA DELTA 1 $quan" . "_PKTS";
    print " 1 0\n";
    print "$schema $quan" . "_STALLED_DELTA DELTA 1 $quan" . "_STALLED";
    print " 1 0\n";
    print "$schema $quan". "_FLITS_PKTS_RATIO DIV_AB 2 ".
	$quan . "_FLITS_DELTA," .
	$quan . "_PKTS_DELTA";
    print " 1 1\n";
    print "$schema $quan". "_STALL_FLITS_RATIO DIV_AB 2 ".
	$quan . "_STALLED_DELTA," .
	$quan . "_FLITS_DELTA";
    print " 1 1\n";
    print "##\n";
}

# PAGE 19
$nquan = "AR_NL_PRF_REQ_NIC";
for ($num = 0; $num < 4; $num++){
    $schema = "rtr_mmr_". $num;
    $quan = $nquan . "_" . $num . "_TO_PTILES";
    print "$schema $quan" . "_FLITS_DELTA DELTA 1 $quan" . "_FLITS";
    print " 1 0\n";
    print "$schema $quan" . "_PKTS_DELTA DELTA 1 $quan" . "_PKTS";
    print " 1 0\n";
    print "$schema $quan" . "_STALLED_DELTA DELTA 1 $quan" . "_STALLED";
    print " 1 0\n";
    print "$schema $quan" . "_FLITS_PKTS_RATIO DIV_AB 2 ".
	$quan . "_FLITS_DELTA," .
	$quan . "_PKTS_DELTA";
    print " 1 1\n";
    print "$schema $quan" . "_STALL_FLITS_RATIO DIV_AB 2 ".
	$quan . "_STALLED_DELTA," .
	$quan . "_FLITS_DELTA";
    print " 1 1\n";
    print "##\n";
}

# PAGE 19
$nquan = "AR_NL_PRF_RSP_NIC";
for ($num = 0; $num < 4; $num++){
    $schema = "rtr_mmr_". $num;
    $quan = $nquan . "_" . $num . "_TO_PTILES";
    print "$schema $quan" . "_FLITS_DELTA DELTA 1 $quan" . "_FLITS";
    print " 1 0\n";
    print "$schema $quan" . "_PKTS_DELTA DELTA 1 $quan" . "_PKTS";
    print " 1 0\n";
    print "$schema $quan" . "_STALLED_DELTA DELTA 1 $quan" . "_STALLED";
    print " 1 0\n";
    print "$schema $quan" . "_FLITS_PKTS_RATIO DIV_AB 2 ".
	$quan . "_FLITS_DELTA," .
	$quan . "_PKTS_DELTA";
    print " 1 1\n";
    print "$schema $quan" . "_STALL_FLITS_RATIO DIV_AB 2 ".
	$quan . "_STALLED_DELTA," .
	$quan . "_FLITS_DELTA";
    print " 1 1\n";
    print "##\n";
}


#PAGE 33
for ($num = 0; $num < 4; $num++){
    $schema = "rtr_mmr_". $num;
    for ($dir = 0; $dir < 2; $dir++){
	$dirstr = "IN";
	if ($dir == 1){
	    $dirstr = "OUT";
	}
	for ($redir = 0; $redir < 2; $redir++){
	    $redirstr = "REQ";
	    if ($redir == 1){
		$redirstr = "RSP";
	    }
	    for ($pp = 0; $pp < 2; $pp++){
		$ptile = $num*2+$pp;
		$quan = "AR_NL_PRF_PTILE_" . $ptile . "_" . $redirstr . "_" . $dirstr . "_NW";
		print "$schema $quan" . "_FLITS_DELTA DELTA 1 $quan" . "_FLITS";
		print " 1 0\n";
		print "$schema $quan" . "_PKTS_DELTA DELTA 1 $quan" . "_PKTS";
		print " 1 0\n";
		print "$schema $quan" . "_FLITS_PKTS_RATIO DIV_AB 2 ".
		    $quan . "_FLITS_DELTA,".
		    $quan . "_PKTS_DELTA";
		print " 1 1\n";
		print "##\n";
	    }
	}
    }
    print "##\n";
}


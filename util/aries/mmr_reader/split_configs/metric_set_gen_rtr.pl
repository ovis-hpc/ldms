#!/usr/bin/perl

# for no redundancy:
# --------------------
# metric_set_rtr_0:
# NIC related = 0
# PTILE related = 0,1
# AR_RTR_(0,1,2,3,4)_(0,1)
#
# metric_set_rtr_1:
# NIC related = 1
# PTILE related = 2,3
# AR_RTR_(0,1,2,3,4)_(2,3)
#
# metric_set_rtr_2:
# NIC related = 2
# PTILE related = 4,5
# AR_RTR_(0,1,2,3,4)_(4,5)
#
# metric_set_rtr_3:
# NIC related = 3
# PTILE related = 6,7
# AR_RTR_(0,1,2,3,4)_(6,7)
#
#
# for redundancy (c):
# --------------------
# metric_set_rtr_0_2 and metric_set_rtr_1_2:
# NIC related = 0,1
# PTILE related = 0,1,2,3
# AR_RTR_(0,1,2,3,4)_(0,1,2,3)
#
# metric_set_rtr_2_2 and metric_set_rtr_3_2:
# NIC related = 2,3
# PTILE related = 4,5,6,7
# AR_RTR_(0,1,2,3,4)_(4,5,6,7)
#
#
# for redundancy (s):
# --------------------
# do all of them
#

$delta_before_sum = 0; # recommend that this is 1

$metric_set = "metric_set_rtr_2"; # fill this in
$gen_rtr = 0; # gen_rtr = 1 generates rtr. gen_rtr = 0 generates the function config.

$schema = $metric_set;

$nicmin = -1;
$nicmax = -1;
$ptilemin = -1;
$ptilemax = -1;
$colmin = -1;
$colmax = -1;
$rowmin = 0;
$rowmax = 5;
$vcmin = 0;
$vcmax = 8;

sub set_params{
    if ($metric_set eq "metric_set_rtr_0"){
	$nicmin = 0;
	$nicmax = 1;
	$ptilemin = 0;
	$ptilemax = 2;
	$colmin = 0;
	$colmax = 2;
    }
    if ($metric_set eq "metric_set_rtr_1"){
	$nicmin = 1;
	$nicmax = 2;
	$ptilemin = 2;
	$ptilemax = 4;
	$colmin = 2;
	$colmax = 4;
    }
    if ($metric_set eq "metric_set_rtr_2"){
	$nicmin = 2;
	$nicmax = 3;
	$ptilemin = 4;
	$ptilemax = 6;
	$colmin = 4;
	$colmax = 6;
    }
    if ($metric_set eq "metric_set_rtr_3"){
	$nicmin = 3;
	$nicmax = 4;
	$ptilemin = 6;
	$ptilemax = 8;
	$colmin = 6;
	$colmax = 8;
    }
    if (($metric_set eq "metric_set_rtr_0_2_c") || ($metric_set eq "metric_set_rtr_1_2_c")){
	$nicmin = 0;
	$nicmax = 2;
	$ptilemin = 0;
	$ptilemax = 4;
	$colmin = 0;
	$colmax = 4;
    }
    if (($metric_set eq "metric_set_rtr_2_2_c") || ($metric_set eq "metric_set_rtr_3_2_c")){
	$nicmin = 2;
	$nicmax = 4;
	$ptilemin = 4;
	$ptilemax = 8;
	$colmin = 4;
	$colmax = 8;
    }
    if (($metric_set eq "metric_set_rtr_0_s") || ($metric_set eq "metric_set_rtr_1_s")){
	$nicmin = 0;
	$nicmax = 4;
	$ptilemin = 0;
	$ptilemax = 8;
	$colmin = 0;
	$colmax = 8;
    }
}

set_params();
if ($gen_rtr > 0){
    for ($nic = $nicmin; $nic < $nicmax; $nic++){
	$header = "AR_NL_PRF_";
	print $header . "REQ_PTILES_TO_NIC_" . $nic . "_FLITS\n";
	print $header.  "REQ_PTILES_TO_NIC_" . $nic . "_PKTS\n";
	print $header . "REQ_PTILES_TO_NIC_" . $nic . "_STALLED\n";
	print $header . "RSP_PTILES_TO_NIC_" . $nic . "_FLITS\n";
	print $header . "RSP_PTILES_TO_NIC_" . $nic . "_PKTS\n";
	print $header . "RSP_PTILES_TO_NIC_" . $nic . "_STALLED\n";
	print $header . "REQ_NIC_" . $nic . "_TO_PTILES_FLITS\n";
	print $header . "REQ_NIC_" . $nic . "_TO_PTILES_PKTS\n";
	print $header . "REQ_NIC_" . $nic . "_TO_PTILES_STALLED\n";
	print $header . "RSP_NIC_" . $nic . "_TO_PTILES_FLITS\n";
	print $header . "RSP_NIC_" . $nic . "_TO_PTILES_PKTS\n";
	print $header . "RSP_NIC_" . $nic . "_TO_PTILES_STALLED\n";
    }

    for ($ptile = $ptilemin; $ptile < $ptilemax; $ptile++){
	$header = "AR_NL_PRF_PTILE_";
	print $header . $ptile . "_REQ_IN_NW_FLITS\n";
	print $header . $ptile . "_REQ_IN_NW_PKTS\n";
	print $header . $ptile . "_REQ_OUT_NW_FLITS\n";
	print $header . $ptile . "_REQ_OUT_NW_PKTS\n";
	print $header . $ptile . "_RSP_IN_NW_FLITS\n";
	print $header . $ptile . "_RSP_IN_NW_PKTS\n";
	print $header . $ptile . "_RSP_OUT_NW_FLITS\n";
	print $header . $ptile . "_RSP_OUT_NW_PKTS\n";
    }

    for ($row = $rowmin; $row < $rowmax; $row++){
	for ($col = $colmin; $col < $colmax; $col++){
	    $header = "AR_RTR_" . $row . "_" . $col . "_";
	    for ($vc = $vcmin; $vc < $vcmax; $vc++){
		print $header . "INQ_PRF_INCOMING_FLIT_VC" .$vc . "\n";
		print $header . "INQ_PRF_INCOMING_PKT_VC" .$vc ."_FILTER_FLIT" .$vc . "_CNT\n";
	    }
	    print $header . "INQ_PRF_ROWBUS_STALL_CNT\n";
	}
    }
} else {

    print "$schema aries_rtr_id RAWTERM 1 aries_rtr_id 1 1\n";

######################################
# AR_RTR Sums and Ratios
######################################

    print "##\n";
    print "## AR_RTR\n";
    print "##\n";

    for ($row = $rowmin; $row < $rowmax; $row++){
	for ($col = $colmin; $col < $colmax; $col++){
	    $header = "AR_RTR_". $row . "_" . $col . "_";

	    if (!$delta_before_sum){

		# SUM VC's (PAGE 32)
		print "$schema $header" . "INQ_PRF_INCOMING_FLIT_VC_SUM SUM_N 8 ";
		for ($vc = $vcmin; $vc < $vcmax; $vc++){
		    print $header . "INQ_PRF_INCOMING_FLIT_VC" . $vc;
		    if ($vc != 7){
			print ",";
		    }
		}
		print " 1 1\n";
		print "#\n";

		# SUM VC's (PAGE 33). CHECK THIS -- WHY IS THERE NO DELTA?
		print "$schema $header" . "INQ_PRF_INCOMING_PKT_VC_SUM_FILTER_FLIT_SUM_CNT SUM_N 8 ";
		for ($vc = $vcmin; $vc < $vcmax; $vc++){
		    print $header . "INQ_PRF_INCOMING_PKT_VC" . $vc . "_FILTER_FLIT" . $vc ."_CNT";
		    if ($vc != 7){
			print ",";
		    }
		}
		print " 1 1\n";
		print "#\n";

		# RATIO OF INCREASE IN STALLS TO FLITS (PAGE 33)
		print "$schema $header" . "INQ_PRF_ROWBUS_STALL_CNT_DELTA DELTA 1 " .
		    $header . "INQ_PRF_ROWBUS_STALL_CNT";
		print " 1 0\n";
		print "$schema $header" . "INQ_PRF_INCOMING_FLIT_VC_SUM_DELTA DELTA 1 " .
		    $header . "INQ_PRF_INCOMING_FLIT_VC_SUM";
		print " 1 0\n";
		print "$schema $header" . "STALL_FLIT_RATIO DIV_AB 2 " .
		    $header . "INQ_PRF_ROWBUS_STALL_CNT_DELTA," .
		    $header . "INQ_PRF_INCOMING_FLIT_VC_SUM_DELTA";
		print " 1 1\n";
		print "##\n";

	    } else {

		# DELTA_VC's first (PAGE 32)
		for ($vc = 0; $vc < $vcmax; $vc++){
		    print "$schema $header" . "INQ_PRF_INCOMING_FLIT_VC" . $vc . "_DELTA DELTA 1 " .
			$header . "INQ_PRF_INCOMING_FLIT_VC" . $vc . " 1 1\n";
		}

		print "$schema $header" . "INQ_PRF_INCOMING_FLIT_VC_DELTA_SUM SUM_N 8 ";
		for ($vc = $vcmin; $vc < $vcmax; $vc++){
		    print $header . "INQ_PRF_INCOMING_FLIT_VC" . $vc . "_DELTA";
		    if ($vc != 7){
			print ",";
		    }
		}
		print " 1 1\n";
		print "#\n";

		# SUM VC's (PAGE 33). CHECK THIS -- WHY IS THERE NO DELTA?
		print "$schema $header" . "INQ_PRF_INCOMING_PKT_VC_SUM_FILTER_FLIT_SUM_CNT SUM_N 8 ";
		for ($vc = $vcmin; $vc < $vcmax; $vc++){
		    print $header . "INQ_PRF_INCOMING_PKT_VC" . $vc . "_FILTER_FLIT" . $vc ."_CNT";
		    if ($vc != 7){
			print ",";
		    }
		}
		print " 1 1\n";
		print "#\n";

		# RATIO OF INCREASE IN STALLS TO FLITS (PAGE 33)
		# CHANGE THESE NAMES IF YOU WANT TO PRINT BOTH
		print "$schema $header" . "INQ_PRF_ROWBUS_STALL_CNT_DELTA DELTA 1 " .
		    $header . "INQ_PRF_ROWBUS_STALL_CNT";
		print " 1 0\n";
		print "$schema $header" . "STALL_FLIT_RATIO DIV_AB 2 " .
		    $header . "INQ_PRF_ROWBUS_STALL_CNT_DELTA," .
		    $header . "INQ_PRF_INCOMING_FLIT_VC_DELTA_SUM";
		print " 1 1\n";
		print "##\n";
	    }

	} # col
    } #row

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
    for ($nic = $nicmin; $nic < $nicmax; $nic++){

	$quan = $nquan . "_" . $nic . "_";
	print "$schema $quan" . "FLITS_DELTA DELTA 1 $quan" . "FLITS";
	print " 1 0\n";

	print "$schema $quan" . "PKTS_DELTA DELTA 1 $quan" . "PKTS";
	print " 1 0\n";

	print "$schema $quan" . "STALLED_DELTA DELTA 1 $quan" . "STALLED";
	print " 1 0\n";

	print "$schema $quan". "FLITS_PKTS_RATIO DIV_AB 2 ".
	    $quan . "FLITS_DELTA," .
	    $quan . "PKTS_DELTA";
	print " 1 1\n";

	print "$schema $quan". "STALL_FLITS_RATIO DIV_AB 2 ".
	    $quan . "STALLED_DELTA," .
	    $quan . "FLITS_DELTA";
	print " 1 1\n";
	print "##\n";

    } #nic

# PAGE 18
    $nquan = "AR_NL_PRF_RSP_PTILES_TO_NIC";
    for ($nic = $nicmin; $nic < $nicmax; $nic++){
	$quan = $nquan . "_" . $nic . "_";
	print "$schema $quan" . "FLITS_DELTA DELTA 1 $quan" . "FLITS";
	print " 1 0\n";
	print "$schema $quan" . "PKTS_DELTA DELTA 1 $quan" . "PKTS";
	print " 1 0\n";
	print "$schema $quan" . "STALLED_DELTA DELTA 1 $quan" . "STALLED";
	print " 1 0\n";
	print "$schema $quan". "FLITS_PKTS_RATIO DIV_AB 2 ".
	    $quan . "FLITS_DELTA," .
	    $quan . "PKTS_DELTA";
	print " 1 1\n";
	print "$schema $quan". "STALL_FLITS_RATIO DIV_AB 2 ".
	    $quan . "STALLED_DELTA," .
	    $quan . "FLITS_DELTA";
	print " 1 1\n";
	print "##\n";
    } #nic

# PAGE 19
    $nquan = "AR_NL_PRF_REQ_NIC";
    for ($nic = $nicmin; $nic < $nicmax; $nic++){
	$quan = $nquan . "_" . $nic . "_TO_PTILES_";
	print "$schema $quan" . "FLITS_DELTA DELTA 1 $quan" . "FLITS";
	print " 1 0\n";
	print "$schema $quan" . "PKTS_DELTA DELTA 1 $quan" . "PKTS";
	print " 1 0\n";
	print "$schema $quan" . "STALLED_DELTA DELTA 1 $quan" . "STALLED";
	print " 1 0\n";
	print "$schema $quan" . "FLITS_PKTS_RATIO DIV_AB 2 ".
	    $quan . "FLITS_DELTA," .
	    $quan . "PKTS_DELTA";
	print " 1 1\n";
	print "$schema $quan" . "STALL_FLITS_RATIO DIV_AB 2 ".
	    $quan . "STALLED_DELTA," .
	    $quan . "FLITS_DELTA";
	print " 1 1\n";
	print "##\n";
    } #nic

# PAGE 19
    $nquan = "AR_NL_PRF_RSP_NIC";
    for ($nic = $nicmin; $nic < $nicmax; $nic++){
	$quan = $nquan . "_" . $nic . "_TO_PTILES_";
	print "$schema $quan" . "FLITS_DELTA DELTA 1 $quan" . "FLITS";
	print " 1 0\n";

	print "$schema $quan" . "PKTS_DELTA DELTA 1 $quan" . "PKTS";
	print " 1 0\n";

	print "$schema $quan" . "STALLED_DELTA DELTA 1 $quan" . "STALLED";
	print " 1 0\n";

	print "$schema $quan" . "FLITS_PKTS_RATIO DIV_AB 2 ".
	    $quan . "FLITS_DELTA," .
	    $quan . "PKTS_DELTA";
	print " 1 1\n";

	print "$schema $quan" . "STALL_FLITS_RATIO DIV_AB 2 ".
	    $quan . "STALLED_DELTA," .
	    $quan . "FLITS_DELTA";
	print " 1 1\n";
	print "##\n";
    } # nic


#PAGE 33
    for ($i = 0; $i < 2; $i++){
	for ($j = 0; $j < 2; $j++){
	    if (($i == 0) && ($j == 0)){
		$str = "REQ_IN";
	    }
	    if (($i == 0) && ($j == 1)){
		$str = "REQ_OUT";
	    }
	    if (($i == 1) && ($j == 0)){
		$str = "RSP_IN";
	    }
	    if (($i == 1) && ($j == 1)){
		$str = "RSP_OUT";
	    }

	    for ($ptile = $ptilemin; $ptile < $ptilemax; $ptile++){
		$quan = "AR_NL_PRF_PTILE_" . $ptile . "_" . $str . "_NW_";
		print "$schema $quan" . "FLITS_DELTA DELTA 1 $quan" . "FLITS";
		print " 1 0\n";
		print "$schema $quan" . "PKTS_DELTA DELTA 1 $quan" . "PKTS";
		print " 1 0\n";
		print "$schema $quan" . "FLITS_PKTS_RATIO DIV_AB 2 ".
		    $quan . "FLITS_DELTA,".
		    $quan . "PKTS_DELTA";
		print " 1 1\n";
		print "##\n";
	    }
	}
	print "##\n";
    }
} # else


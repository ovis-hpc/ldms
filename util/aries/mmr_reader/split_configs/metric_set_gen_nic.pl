#!/usr/bin/perl

#########################
# only 1 nic set
#########################

#$metric_set = "metric_set_rtr_0"; # fill this in
$metric_set = "nic_mmr";
$gen_nic = 0; # gen_nic = 1 generates nic. gen_nic = 0 generates the function config.

$schema = $metric_set;
$NIC_STALL_FLITS_SCALE = 100;

if ($gen_nic > 0){

    print "AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_PKTS\n";
    print "AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_FLITS\n";
    print "AR_NIC_NETMON_ORB_EVENT_CNTR_REQ_STALLED\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_PI_PKTS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_PI_FLITS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_PI_STALLED\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_AMO_PKTS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_AMO_FLITS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_AMO_BLOCKED\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_WC_PKTS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_WC_FLITS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_WC_BLOCKED\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_BTE_RD_PKTS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_BTE_RD_FLITS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_BTE_RD_BLOCKED\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_IOMMU_PKTS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_IOMMU_FLITS\n";
    print "AR_NIC_RSPMON_PARB_EVENT_CNTR_IOMMU_BLOCKED\n";

} else {

##################################################################
# NIC
# Pay particular attention to flits (plural) vs flit (singular)
##################################################################
# PAGE 12
@NIC_ARGS = ("AR_NIC_RSPMON_PARB_EVENT_CNTR_AMO","AR_NIC_RSPMON_PARB_EVENT_CNTR_WC",
	     "AR_NIC_RSPMON_PARB_EVENT_CNTR_BTE_RD", "AR_NIC_RSPMON_PARB_EVENT_CNTR_IOMMU");

    print "##\n";
    print "## NIC\n";
    print "##\n";

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
}


#!/bin/bash

hostlist=$1

# Define offset for aggregation interval (in usec)
INTERVAL=1000000

# Define offset for aggregation (in usec)
OFFSET=200000

# Define aggregation transport
#XPRT=ugni
XPRT=sock

# Define port to talk to on remote ldmsd(s) being aggregated from
PORT=411

while read host
do
	echo "prdcr_add name=grp1.${host}.$PORT host=${host} port=$PORT xprt=$XPRT type=active interval=30000000"
	echo "prdcr_start name=grp1.${host}.$PORT"
done < $hostlist

echo "updtr_add name=grp1 interval=$INTERVAL offset=$OFFSET"
echo "updtr_prdcr_add name=grp1 regex=grp1..*"
echo "updtr_start name=grp1"

echo "load name=store_csv"
#echo "load name=store_function_csv"

# FIXME -- define store path (must exist)
echo "config name=store_csv path=<absolute path to CSV store dir>/LDMSD_CSV action=init altheader=1 rollover=3600 rolltype=1"
#echo "config name=store_function_csv path=<absolute path to CSV store dir>/LDMSD_CSV altheader=1 rollover=3600 rolltype=1 derivedconf=<absolute path to function store configuration file>/function_store.conf"

echo "strgp_add name=cray_aries_r_store_csv plugin=store_csv container=csv schema=cray_aries_r"
#echo "strgp_add name=cray_power_store_csv plugin=store_csv container=csv schema=cray_power_sampler"
#echo "strgp_add name=meminfo_store_csv plugin=store_csv container=csv schema=meminfo"
#echo "strgp_add name=vmstat_store_csv plugin=store_csv container=csv schema=vmstat"
#echo "strgp_add name=procstat_store_csv plugin=store_csv container=csv schema=procstat"
echo "strgp_add name=aries_rtr_mmr_0_1_c_store_csv plugin=store_csv container=csv schema=metric_set_rtr_0_1_c"
echo "strgp_add name=aries_rtr_mmr_0_1_s_store_csv plugin=store_csv container=csv schema=metric_set_rtr_0_1_s"
echo "strgp_add name=aries_rtr_mmr_0_2_c_store_csv plugin=store_csv container=csv schema=metric_set_rtr_0_2_c"
echo "strgp_add name=aries_rtr_mmr_0_2_s_store_csv plugin=store_csv container=csv schema=metric_set_rtr_0_2_s"
echo "strgp_add name=aries_rtr_mmr_1_1_c_store_csv plugin=store_csv container=csv schema=metric_set_rtr_1_1_c"
echo "strgp_add name=aries_rtr_mmr_1_1_s_store_csv plugin=store_csv container=csv schema=metric_set_rtr_1_1_s"
echo "strgp_add name=aries_rtr_mmr_1_2_c_store_csv plugin=store_csv container=csv schema=metric_set_rtr_1_2_c"
echo "strgp_add name=aries_rtr_mmr_1_2_s_store_csv plugin=store_csv container=csv schema=metric_set_rtr_1_2_s"
echo "strgp_add name=aries_rtr_mmr_2_1_c_store_csv plugin=store_csv container=csv schema=metric_set_rtr_2_1_c"
echo "strgp_add name=aries_rtr_mmr_2_1_s_store_csv plugin=store_csv container=csv schema=metric_set_rtr_2_1_s"
echo "strgp_add name=aries_rtr_mmr_2_2_c_store_csv plugin=store_csv container=csv schema=metric_set_rtr_2_2_c"
echo "strgp_add name=aries_rtr_mmr_2_2_s_store_csv plugin=store_csv container=csv schema=metric_set_rtr_2_2_s"
echo "strgp_add name=aries_rtr_mmr_3_1_c_store_csv plugin=store_csv container=csv schema=metric_set_rtr_3_1_c"
echo "strgp_add name=aries_rtr_mmr_3_1_s_store_csv plugin=store_csv container=csv schema=metric_set_rtr_3_1_s"
echo "strgp_add name=aries_rtr_mmr_3_2_c_store_csv plugin=store_csv container=csv schema=metric_set_rtr_3_2_c"
echo "strgp_add name=aries_rtr_mmr_3_2_s_store_csv plugin=store_csv container=csv schema=metric_set_rtr_3_2_s"
echo "strgp_add name=aries_nic_mmr_store_csv plugin=store_csv container=csv schema=metric_set_nic"

echo "strgp_start name=cray_aries_r_store_csv"
#echo "strgp_start name=cray_power_store_csv"
#echo "strgp_start name=meminfo_store_csv"
#echo "strgp_start name=vmstat_store_csv"
#echo "strgp_start name=procstat_store_csv"
echo "strgp_start name=aries_rtr_mmr_0_1_c_store_csv"
echo "strgp_start name=aries_rtr_mmr_0_1_s_store_csv"
echo "strgp_start name=aries_rtr_mmr_0_2_c_store_csv"
echo "strgp_start name=aries_rtr_mmr_0_2_s_store_csv"
echo "strgp_start name=aries_rtr_mmr_1_1_c_store_csv"
echo "strgp_start name=aries_rtr_mmr_1_1_s_store_csv"
echo "strgp_start name=aries_rtr_mmr_1_2_c_store_csv"
echo "strgp_start name=aries_rtr_mmr_1_2_s_store_csv"
echo "strgp_start name=aries_rtr_mmr_2_1_c_store_csv"
echo "strgp_start name=aries_rtr_mmr_2_1_s_store_csv"
echo "strgp_start name=aries_rtr_mmr_2_2_c_store_csv"
echo "strgp_start name=aries_rtr_mmr_2_2_s_store_csv"
echo "strgp_start name=aries_rtr_mmr_3_1_c_store_csv"
echo "strgp_start name=aries_rtr_mmr_3_1_s_store_csv"
echo "strgp_start name=aries_rtr_mmr_3_2_c_store_csv"
echo "strgp_start name=aries_rtr_mmr_3_2_s_store_csv"
echo "strgp_start name=aries_nic_mmr_store_csv"

#echo "strgp_add name=aries_rtr_mmr_0_1_c_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_0_1_c"
#echo "strgp_add name=aries_rtr_mmr_0_1_s_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_0_1_s"
#echo "strgp_add name=aries_rtr_mmr_0_2_c_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_0_2_c"
#echo "strgp_add name=aries_rtr_mmr_0_2_s_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_0_2_s"
#echo "strgp_add name=aries_rtr_mmr_1_1_c_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_1_1_c"
#echo "strgp_add name=aries_rtr_mmr_1_1_s_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_1_1_s"
#echo "strgp_add name=aries_rtr_mmr_1_2_c_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_1_2_c"
#echo "strgp_add name=aries_rtr_mmr_1_2_s_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_1_2_s"
#echo "strgp_add name=aries_rtr_mmr_2_1_c_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_2_1_c"
#echo "strgp_add name=aries_rtr_mmr_2_1_s_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_2_1_s"
#echo "strgp_add name=aries_rtr_mmr_2_2_c_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_2_2_c"
#echo "strgp_add name=aries_rtr_mmr_2_2_s_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_2_2_s"
#echo "strgp_add name=aries_rtr_mmr_3_1_c_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_3_1_c"
#echo "strgp_add name=aries_rtr_mmr_3_1_s_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_3_1_s"
#echo "strgp_add name=aries_rtr_mmr_3_2_c_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_3_2_c"
#echo "strgp_add name=aries_rtr_mmr_3_2_s_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_rtr_3_2_s"
#echo "strgp_add name=aries_nic_mmr_store_function_csv plugin=store_function_csv container=function_csv schema=metric_set_nic"
#
#echo "strgp_start name=aries_rtr_mmr_0_1_c_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_0_1_s_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_0_2_c_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_0_2_s_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_1_1_c_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_1_1_s_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_1_2_c_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_1_2_s_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_2_1_c_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_2_1_s_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_2_2_c_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_2_2_s_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_3_1_c_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_3_1_s_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_3_2_c_store_function_csv"
#echo "strgp_start name=aries_rtr_mmr_3_2_s_store_function_csv"
#echo "strgp_start name=aries_nic_mmr_store_function_csv"

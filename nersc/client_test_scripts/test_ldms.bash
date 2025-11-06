#!/bin/bash

/opt/ovis-ldms/sbin/ldms_ls -a munge -x sock -h localhost -p 6002
echo "Press enter to continue"; 
read FOO; 

foo="stream_status thread_stats set_stats update_time_stats strgp_status prdcr_stats updtr_status 'updtr_status summary' daemon_status"

for i in $foo; do
 echo "============================================
$i
=====================================";
 /opt/ovis-ldms/bin/ldmsd_controller -a munge  -A socket=/var/run/munge/munge.socket.2 -x sock -h localhost -p 6002 --cmd $i
 echo "Press enter to continue"; 
 read FOO; 
done

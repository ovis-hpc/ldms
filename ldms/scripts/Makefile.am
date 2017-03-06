
# consider adding ldmsd to bin_SCRIPTS if ldmsd script ever stabilizes
EXTRA_DIST=@OVIS_EXTRA_DIST@

if ENABLE_SCRIPTS
bin_SCRIPTS= envldms.sh \
	ldms_local_clocktest.sh \
	ldms_local_usertest.sh \
	ldms_usertest.sh \
	ldms_local_valgrind.sh \
	ldms_local_inspxe.sh \
	ldms_local_inspxe_rdma.sh \
	ldms_local_rdmatest.sh

# CLEANFILES = $(bin_SCRIPTS)

bin_SCRIPTS+= ldms_ban.sh

bin_SCRIPTS+=  ldms_ctltest.sh
bin_SCRIPTS+=  ldms_local_csvorder.sh
bin_SCRIPTS+=  ldms_local_jobidmost.sh
bin_SCRIPTS+=  ldms_local_jobidtest.sh

bin_SCRIPTS+= \
ldms_local_rollbytes_test.sh \
ldms_local_rollperiod_test.sh \
ldms_local_rollrecords_test.sh \
ldms_local_rolltime_test.sh

bin_SCRIPTS += ldms_local_syslog.sh

bin_SCRIPTS += ldms_local_synthetic_test.sh
bin_SCRIPTS += ldms_local_auth_test.sh
bin_SCRIPTS += ldms_local_amqptest.sh
bin_SCRIPTS += ldms_local_sostest.sh

bin_SCRIPTS += ldms_local_lnet_statstest.sh

bin_SCRIPTS += ldms_plugins_list.sh
bin_SCRIPTS += ldms-py-edac_test.sh
bin_SCRIPTS += ldms-py-subset_test.sh
endif


# consider adding ldmsd to bin_SCRIPTS if ldmsd script ever stabilizes
bin_SCRIPTS= envldms.sh \
	ldms_local_clocktest.sh \
	ldms_local_usertest.sh \
	ldms_usertest.sh \
	ldms_local_valgrind.sh \
	ldms_local_inspxe.sh \
	ldms_local_inspxe_rdma.sh \
	ldms_local_rdmatest.sh

CLEANFILES = $(bin_SCRIPTS)

bin_SCRIPTS+= ldms_ban.sh

bin_SCRIPTS+=  ldms_ctltest.sh
bin_SCRIPTS+=  ldms_local_slurmtest.sh

bin_SCRIPTS+= \
ldms_local_rollbytes_test.sh \
ldms_local_rollperiod_test.sh \
ldms_local_rollrecords_test.sh \
ldms_local_rolltime_test.sh

bin_SCRIPTS+= \
ldms_local_dertest.sh \
ldms_local_dertest_big.sh

bin_SCRIPTS += ldms_local_procstatutil.sh

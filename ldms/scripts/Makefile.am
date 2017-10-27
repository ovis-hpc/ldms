
# consider adding ldmsd to bin_SCRIPTS if ldmsd script ever stabilizes
EXTRA_DIST=@OVIS_EXTRA_DIST@

if ENABLE_SCRIPTS
bin_SCRIPTS= envldms.sh

DISTCLEANFILES = $(bin_SCRIPTS)

bin_SCRIPTS+= ldms_ban.sh
bin_SCRIPTS+=  ldms-py-fptrans_test.sh
bin_SCRIPTS += ldms_plugins_list.sh
bin_SCRIPTS += ldms-py-edac_test.sh
bin_SCRIPTS += ldms-py-subset_test.sh
bin_SCRIPTS += ldms-py-syslog.sh
bin_SCRIPTS += ldms-meminfo.sh
bin_SCRIPTS += ldms-py-varset.sh
bin_SCRIPTS += ldms-py-rename.sh
endif

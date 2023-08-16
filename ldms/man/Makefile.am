EXTRA_DIST=

dist_man1_MANS= \
ldms-csv-anonymize.man \
ldms-csv-export-sos.man \
lsdate.man

dist_man8_MANS= \
ldms_build_install.man \
ldms_ls.man \
ldms-static-test.man \
ldmsd.man \
ldms-plugins.man \
ldmsd_exits.man \
ldmsd_controller.man \
ldmsctl.man

dist_man7_MANS= \
ldms_authentication.man \
ldms_auth_none.man \
ldms_auth_ovis.man \
ldms_auth_munge.man \
ldms_auth_naive.man \
ldms_quickstart.man \
ldms_sampler_base.man \
ldmsd_failover.man \
ldmsd_setgroup.man \
Plugin_jobid.man \
Plugin_store_csv.man \
Plugin_store_sos.man \
Plugin_store_papi.man \
Plugin_store_app.man \
ldmsd_decomposition.man \
Plugin_store_kafka.man

if ENABLE_SLURMTEST
dist_man8_MANS += pll-ldms-static-test.man
endif
EXTRA_DIST += pll-ldms-static-test.man

dist_man7_MANS += Plugin_store_rabbitv3.man
dist_man7_MANS += Plugin_store_rabbitkw.man

ldmsd_exits.man: $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c
	$(srcdir)/make_exits_man.sh $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c > ldmsd_exits.man

CLEANFILES=ldmsd_exits.man

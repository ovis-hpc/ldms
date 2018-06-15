
dist_man8_MANS= \
ldms_build_install.man \
ldms_ls.man \
ldms-static-test.man \
ldmsd.man \
ldmsd_exits.man \
ldms-static-test.man \
ldmsd_controller.man \
ldmsctl.man

dist_man7_MANS= \
ldms_authentication.man \
ldms_quickstart.man \
Plugin_aries_mmr.man \
Plugin_cray_system_sampler_variants.man \
Plugin_kgnilnd.man \
Plugin_knc_sampler.man \
Plugin_lnet_stats.man \
Plugin_meminfo.man \
Plugin_fptrans.man \
Plugin_clock.man \
Plugin_procstat.man \
Plugin_opa2.man \
Plugin_sysclassib.man \
Plugin_jobid.man \
Plugin_lustre2_client.man \
Plugin_msr_interlagos.man \
Plugin_procinterrupts.man \
Plugin_procnetdev.man \
Plugin_procnfs.man \
Plugin_store_csv.man \
Plugin_store_flatfile.man \
Plugin_store_sos.man \
Plugin_vmstat.man

if ENABLE_ARRAY_EXAMPLE
dist_man7_MANS += Plugin_all_example.man
endif

pending_dist_man7_MANS= \
Plugin_procsensors.man

dist_man7_MANS += Plugin_store_rabbitv3.man
dist_man7_MANS += Plugin_store_rabbitkw.man

ldmsd_exits.man: $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c
	$(srcdir)/make_exits_man.sh $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c > ldmsd_exits.man

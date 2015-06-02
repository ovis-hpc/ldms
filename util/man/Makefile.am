EXTRA_DIST=ldms.pod

dist_man8_MANS= \
ldmsctl.man \
ldmsd.man \
ldms_ls.man

dist_man7_MANS= \
ldms.man \
LDMS_Authentication.man \
LDMS_QuickStart.man \
Plugin_cray_system_sampler_variants.man \
Plugin_kgnilnd.man \
Plugin_lustre2_client.man \
Plugin_meminfo.man \
Plugin_procnetdev.man \
Plugin_procnfs.man \
Plugin_procsensors.man \
Plugin_procstatutil.man \
Plugin_procstatutil2.man \
Plugin_slurmjobid.man \
Plugin_store_csv.man \
Plugin_store_derived_csv.man \
Plugin_sysclassib.man \
Plugin_vmstat.man

ldms.man: ldms.pod
	pod2man -n ldms --section=7 --release=@VERSION@ -c "LDMS Manual Index" < $(srcdir)/ldms.pod > ldms.man


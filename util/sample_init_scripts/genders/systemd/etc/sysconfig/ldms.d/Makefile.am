docdir = $(datadir)/doc/@PACKAGE@-@VERSION@/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d

if ENABLE_TOSS_SYSTEMD
SUBDIRS = plugins-conf ClusterGenders ClusterSecrets debug
doc_DATA = \
ldmsd \
ldms-functions
dist_doc_DATA = \
ldmsd.local.conf \
ldmsd.agg.conf \
ldmsd.all_instances.conf.example \
README
endif


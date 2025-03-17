include $(top_srcdir)/ldms/rules.mk

EXTRA_DIST=

dist_man1_MANS= \
ldms-lsdate.man

dist_man8_MANS= \
ldms-authentication.man \
ldms-build-install.man \
ldms-plugins.man \
ldmsd-exits.man \
ldmsd-controller.man \
ldmsd-yaml-parser.man \
ldms-stream.man

dist_man7_MANS= \
ldmsd-qgroup.man \
ldms-quickstart.man \
ldms-reverse-conf.man \
ldms-csv-time-drops.man \
ldmsd-sampler-advertisement.man \
ldmsd-peer-daemon-advertisement.man

ldmsd-exits.man: $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c
	$(srcdir)/make_exits_man.sh $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c > ldmsd-exits.man

CLEANFILES=ldmsd-exits.man

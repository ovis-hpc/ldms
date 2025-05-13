include $(top_srcdir)/ldms/rules.mk

EXTRA_DIST=

dist_man1_MANS= \
ldms_lsdate.man

dist_man8_MANS= \
ldms_authentication.man \
ldms_build_install.man \
ldms-plugins.man \
ldmsd-exits.man \
ldmsd_controller.man \
ldmsd_yaml_parser.man \
ldms_stream.man

dist_man7_MANS= \
ldmsd_qgroup.man \
ldms_quickstart.man \
ldms-reverse-conf.man \
ldmsd_peer_daemon_advertisement.man

ldmsd-exits.man: $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c
	$(srcdir)/make_exits_man.sh $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c > ldmsd-exits.man

CLEANFILES=ldmsd-exits.man

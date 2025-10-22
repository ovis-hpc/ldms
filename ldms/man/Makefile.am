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
ldms_msg.man

dist_man7_MANS= \
ldmsd_qgroup.man \
ldms_quickstart.man \
ldms-reverse-conf.man \
ldmsd_peer_daemon_advertisement.man \
ldms_msg.man \
ldmsd_stream_ldms_msg_conversion_guide.man

ldmsd-exits.man: $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c
	$(srcdir)/make_exits_man.sh $(srcdir)/../src/ldmsd/ldmsd.c $(srcdir)/../src/ldmsd/ldmsd_config.c > ldmsd-exits.man

CLEANFILES=$(dist_man1_MANS) $(dist_man7_MANS) $(dist_man8_MANS)

EXTRA_DIST+=ldms_lsdate.rst
EXTRA_DIST+=ldms_authentication.rst
EXTRA_DIST+=ldms_build_install.rst
EXTRA_DIST+=ldms-plugins.rst
EXTRA_DIST+=ldmsd_controller.rst
EXTRA_DIST+=ldmsd_yaml_parser.rst
EXTRA_DIST+=ldms_msg.rst
EXTRA_DIST+=ldmsd_qgroup.rst
EXTRA_DIST+=ldms_quickstart.rst
EXTRA_DIST+=ldms-reverse-conf.rst
EXTRA_DIST+=ldmsd_peer_daemon_advertisement.rst
EXTRA_DIST+=ldms_msg.rst
EXTRA_DIST+=ldmsd_stream_ldms_msg_conversion_guide.rst

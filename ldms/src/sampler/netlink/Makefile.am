pkglib_LTLIBRARIES =
dist_man7_MANS=
dist_man8_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/ldmsd/libldms_sps.la \
		$(top_builddir)/ldms/src/ldmsd/libldmsd_stream.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la

if HAVE_NETLINK
sbin_PROGRAMS = ldms-netlink-notifier
dist_man8_MANS += netlink-notifier.man
ldms_netlink_notifier_CFLAGS = $(AM_CFLAGS)
ldms_netlink_notifier_SOURCES = netlink-notifier.c
ldms_netlink_notifier_LDADD = $(COMMON_LIBADD) -lpthread -lm
endif

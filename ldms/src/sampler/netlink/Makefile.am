include $(top_srcdir)/ldms/rules.mk


lib_LTLIBRARIES =
dist_man7_MANS=
dist_man8_MANS=
sbin_PROGRAMS =
EXTRA_PROGRAMS = test_slps

AM_CPPFLAGS = \
	-I. \
	-I$(abs_srcdir) \
	-I$(abs_srcdir)/../../../../lib/src

AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/ldmsd/libldmsd_stream.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/ovis_json/libovis_json.la

slpsincludedir = $(includedir)/slps/
slpsinclude_HEADERS = simple_lps.h

if HAVE_NETLINK
# ldms-netlink-notifier does not use ldms headers except ovis json.
sbin_PROGRAMS += ldms-netlink-notifier
dist_man8_MANS += ldms-sampler_netlink-notifier.man ldms-sampler_ldms-notify.man
ldms_netlink_notifier_CFLAGS = $(AM_CFLAGS)
ldms_netlink_notifier_SOURCES = netlink-notifier.c
ldms_netlink_notifier_LDADD = libsimple_lps.la $(COMMON_LIBADD) -lpthread -lm
endif

lib_LTLIBRARIES += libsimple_lps.la
libsimple_lps_la_SOURCES = simple_lps.c simple_lps.h
libsimple_lps_la_LIBADD = $(COMMON_LIBADD)
libsimple_lps_la_CFLAGS = $(AM_CFLAGS) @OVIS_INCLUDE_ABS@

# test_slps does not use ldms headers except ovis json.
test_slps_SOURCES = test_simple_lps.c simple_lps.h
test_slps_CFLAGS = $(AM_CFLAGS)
test_slps_LDADD = libsimple_lps.la $(COMMON_LIBADD) \
		 -lm -lpthread

EXTRA_DIST = $(srcdir)/input/test_slps.sh

distclean-local:
	$(RM) test_slps.* $(EXTRA_PROGRAMS)

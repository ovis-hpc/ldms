include $(top_srcdir)/ldms/rules.mk


ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES =
pkglib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =

@LIB_MAKE_VARS@
@LDMS_MAKE_VARS@

# set no_ban to any non-empty value to stop ban checking
NO_BAN=

AM_CFLAGS = -I$(top_srcdir)/include -I.. -I$(top_srcdir) \
	-I$(ovis_ldms_includedir) \
	-I$(ovis_ldms_includedir)/ldms \
	-I$(ovis_ldms_includedir)/ldms/sampler

COMMON_LIBADD = -lldms -lovis_util -lsampler_base

if ENABLE_MY_PLUGIN
libmy_plugin_la_SOURCES = my_plugin.c
libmy_plugin_CFLAGS = $(AM_CFLAGS)
libmy_plugin_la_LDFLAGS = $(AM_LDFLAGS) -module
libmy_plugin_la_LIBADD = $(COMMON_LIBADD)
dist_man7_MANS += ldms_my_plugin.man
pkglib_LTLIBRARIES += libmy_plugin.la
endif

all-local: always_check

always_check:
	@echo checking for banned libs that foul up plugin libtool linkage
	@../scripts/check_banned.sh $(NO_BAN)

rm_banned:
	../scripts/rm_banned.sh

.PHONY: always_check

CLEANFILES = $(dist_man7_MANS)

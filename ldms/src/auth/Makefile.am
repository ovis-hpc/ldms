include $(top_srcdir)/ldms/rules.mk


lib_LTLIBRARIES =
dist_man7_MANS =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

AM_CFLAGS = $(DBGFLAGS) -DSYSCONFDIR='"$(sysconfdir)"' -DPLUGINDIR='"$(pkglibdir)"'
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""


libldms_auth_none_la_SOURCES = ldms_auth_none.c
libldms_auth_none_la_LIBADD = ../core/libldms.la
lib_LTLIBRARIES += libldms_auth_none.la

libldms_auth_naive_la_SOURCES = ldms_auth_naive.c
libldms_auth_naive_la_LIBADD = ../core/libldms.la \
			       $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			       $(top_builddir)/lib/src/ovis_log/libovis_log.la

lib_LTLIBRARIES += libldms_auth_naive.la

libldms_auth_ovis_la_SOURCES = ldms_auth_ovis.c
libldms_auth_ovis_la_LIBADD = ../core/libldms.la \
			      $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			      $(top_builddir)/lib/src/ovis_auth/libovis_auth.la \
			      $(top_builddir)/lib/src/ovis_log/libovis_log.la
lib_LTLIBRARIES += libldms_auth_ovis.la

if HAVE_MUNGE
libldms_auth_munge_la_SOURCES = ldms_auth_munge.c
libldms_auth_munge_la_CFLAGS = @MUNGE_INCDIR_FLAG@
libldms_auth_munge_la_LIBADD = ../core/libldms.la \
			       $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			       -lmunge \
			       @MUNGE_LIB64DIR_FLAG@ \
			       @MUNGE_LIBDIR_FLAG@ \
			       $(top_builddir)/lib/src/ovis_log/libovis_log.la
lib_LTLIBRARIES += libldms_auth_munge.la
dist_man7_MANS += ldms_auth_munge.man
endif

dist_man7_MANS += ldms_auth_none.man \
		  ldms_auth_ovis.man \
		  ldms_auth_naive.man

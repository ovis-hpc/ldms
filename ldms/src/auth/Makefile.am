lib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

AM_CFLAGS = $(DBGFLAGS) -DSYSCONFDIR='"$(sysconfdir)"' -DPLUGINDIR='"$(pkglibdir)"'
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""


libldms_auth_none_la_SOURCES = ldms_auth_none.c
libldms_auth_none_la_LIBADD = -lldms
lib_LTLIBRARIES += libldms_auth_none.la

libldms_auth_naive_la_SOURCES = ldms_auth_naive.c
libldms_auth_naive_la_LIBADD = -lldms -lovis_util

lib_LTLIBRARIES += libldms_auth_naive.la

libldms_auth_ovis_la_SOURCES = ldms_auth_ovis.c
libldms_auth_ovis_la_LIBADD = -lldms -lovis_util -lovis_auth
lib_LTLIBRARIES += libldms_auth_ovis.la

if ENABLE_MUNGE
libldms_auth_munge_la_SOURCES = ldms_auth_munge.c
libldms_auth_munge_la_CFLAGS = @MUNGE_INCDIR_FLAG@
libldms_auth_munge_la_LIBADD = -lldms \
			       -lovis_util \
			       -lmunge \
			       @MUNGE_LIB64DIR_FLAG@ \
			       @MUNGE_LIBDIR_FLAG@
lib_LTLIBRARIES += libldms_auth_munge.la
endif

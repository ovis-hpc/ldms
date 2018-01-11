lib_LTLIBRARIES =

AM_CFLAGS = $(DBGFLAGS) -I$(top_srcdir) -I.. @OVIS_LIB_INCDIR_FLAG@
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"' -DPLUGINDIR='"$(pkglibdir)"'
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

libldms_auth_none_la_SOURCES = ldms_auth_none.c
libldms_auth_none_la_LIBADD = ../core/libldms.la \
			      @OVIS_LIB_LIB64DIR_FLAG@ \
			      @OVIS_LIB_LIBDIR_FLAG@
lib_LTLIBRARIES += libldms_auth_none.la

libldms_auth_naive_la_SOURCES = ldms_auth_naive.c
libldms_auth_naive_la_LIBADD = ../core/libldms.la \
			       -lovis_util \
			       @OVIS_LIB_LIB64DIR_FLAG@ \
			       @OVIS_LIB_LIBDIR_FLAG@
lib_LTLIBRARIES += libldms_auth_naive.la

libldms_auth_ovis_la_SOURCES = ldms_auth_ovis.c
libldms_auth_ovis_la_LIBADD = ../core/libldms.la \
			       -lovis_util \
			       -lovis_auth \
			       @OVIS_LIB_LIB64DIR_FLAG@ \
			       @OVIS_LIB_LIBDIR_FLAG@
lib_LTLIBRARIES += libldms_auth_ovis.la

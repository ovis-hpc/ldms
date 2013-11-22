pkglib_LTLIBRARIES =

AM_CFLAGS = -Wall $(DBGFLAGS) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

mecoreincludedir = $(includedir)/me
mecoreinclude_HEADERS = me.h

libme_la_SOURCES = me_util.c me.h me_priv.h
libme_la_LIBADD = -lovis_util
pkglib_LTLIBRARIES += libme.la

libme_core_la_SOURCES = me_util.c me_interface.c model_manager.c \
			me_interface.h me_interface_priv.h \
			model_manager.h me_priv.h me.h \
			hash_rbt.c hash_rbt.h
libme_core_la_LIBADD = -lzap -lcoll -lovis_util
pkglib_LTLIBRARIES += libme_core.la

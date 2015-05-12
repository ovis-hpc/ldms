lib_LTLIBRARIES =

AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@ -lpthread -lzap \
	     -lovis_util

#AM_CFLAGS = -I$(srcdir)/../ @OVIS_LIB_INCDIR_FLAG@
#AM_LDFLAGS = -lovis_util @OVIS_LIB_LIBDIR_FLAG@ -lpthread -lovis_util

libovis_rctrl_la_SOURCES = rctrl.c rctrl.h
libovis_rctrl_la_LIBADD = ../libocm.la
libovis_rctrl_la_CFLAGS = -I$(srcdir)/../ @OVIS_LIB_INCDIR_FLAG@
libovis_rctrl_la_LDFLAGS = -lovis_util @OVIS_LIB_LIBDIR_FLAG@ -lpthread -lovis_util
libovis_rctrlinclude_HEADERS =  rctrl.h
libovis_rctrlincludedir = $(includedir)/ovis_rctrl
libovis_rctrl_conf = /etc/ld.so.conf.d/libovis_rctrl.conf
lib_LTLIBRARIES += libovis_rctrl.la

#install-exec-hook:
#	echo $(libdir) > $(libovis_rctrl_conf)
#	ldconfig
#
#uninstall-local:
#	rm -f $(libovis_rctrl_conf)
#	ldconfig

lib_LTLIBRARIES =

libovis_rctrl_la_SOURCES = rctrl.c rctrl.h
libovis_rctrl_la_LIBADD = ../ocm/libocm.la -lovis_util @OVIS_LIB_LIBDIR_FLAG@ \
			  -lpthread -lovis_util
libovis_rctrl_la_CFLAGS = -I$(srcdir)/../ @OVIS_LIB_INCDIR_FLAG@

if ENABLE_AUTH
libovis_rctrl_la_CFLAGS += -DENABLE_AUTH
libovis_rctrl_la_LIBADD += -lovis_auth
endif

libovis_rctrlinclude_HEADERS =  rctrl.h
libovis_rctrlincludedir = $(includedir)/ovis_rctrl

lib_LTLIBRARIES += libovis_rctrl.la

#install-exec-hook:
#	echo $(libdir) > $(libovis_rctrl_conf)
#	ldconfig
#
#uninstall-local:
#	rm -f $(libovis_rctrl_conf)
#	ldconfig

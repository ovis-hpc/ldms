lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)

if ENABLE_OVIS_CTRL
libovis_ctrl_la_SOURCES = ctrl.c ctrl.h
libovis_ctrlinclude_HEADERS =  ctrl.h
libovis_ctrlincludedir = $(includedir)/ovis_ctrl
libovis_ctrl_conf = /etc/ld.so.conf.d/libovis_ctrl.conf
lib_LTLIBRARIES += libovis_ctrl.la
endif

#install-exec-hook:
#	echo $(libdir) > $(libovis_ctrl_conf)
#	ldconfig
#
#uninstall-local:
#	rm -f $(libovis_ctrl_conf)
#	ldconfig

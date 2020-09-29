lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir) -I..

if ENABLE_OVIS_CTRL
libovis_ctrl_la_SOURCES = ctrl.c ctrl.h
libovis_ctrl_la_LIBADD = ../ovis_util/libovis_util.la
libovis_ctrlinclude_HEADERS =  ctrl.h
libovis_ctrlincludedir = $(includedir)/ovis_ctrl
libovis_ctrl_conf = /etc/ld.so.conf.d/libovis_ctrl.conf
lib_LTLIBRARIES += libovis_ctrl.la
endif

SUBDIRS = . test/

lib_LTLIBRARIES =
AM_CFLAGS = -I@top_srcdir@/src/

libbin_rsyslog_tcp_la_SOURCES = bin_rsyslog_tcp.c
libbin_rsyslog_tcp_la_LIBADD = ../baler/libbutils.la -levent \
			       -levent_pthreads \
			       ../baler/libbaler.la
lib_LTLIBRARIES += libbin_rsyslog_tcp.la

libbout_sos_la_SOURCES = bout_sos.c
libbout_sos_la_CFLAGS = $(AM_CFLAGS) -I@SOS_INCDIR@
libbout_sos_la_LIBADD = ../baler/libbutils.la -lsos ../baler/libbaler.la \
			-L@SOS_LIBDIR@
lib_LTLIBRARIES += libbout_sos.la

libbout_sos_img_la_SOURCES = bout_sos_img.c
libbout_sos_img_la_CFLAGS = $(AM_CFLAGS) -I@SOS_INCDIR@
libbout_sos_img_la_LIBADD = ../baler/libbutils.la ../baler/libbaler.la \
			    libbout_sos.la \
			    -lsos -L@SOS_LIBDIR@
lib_LTLIBRARIES += libbout_sos_img.la

libbout_sos_msg_la_SOURCES = bout_sos_msg.c
libbout_sos_msg_la_CFLAGS = $(AM_CFLAGS) -I@SOS_INCDIR@
libbout_sos_msg_la_LIBADD = ../baler/libbutils.la ../baler/libbaler.la \
			    libbout_sos.la \
			    -lsos -L@SOS_LIBDIR@
lib_LTLIBRARIES += libbout_sos_msg.la

libbout_me_la_SOURCES = bout_me.c
libbout_me_la_CFLAGS = $(AM_CFLAGS) -I@OVIS_LIB_INCDIR@
libbout_me_la_LIBADD = ../baler/libbutils.la ../baler/libbaler.la \
		       -levent -levent_pthreads -lpthread -lzap \
		       -L@OVIS_LIB_LIBDIR@ \
		       -L@OVIS_LIB_LIB64DIR@
lib_LTLIBRARIES += libbout_me.la


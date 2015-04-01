SUBDIRS = . test/

lib_LTLIBRARIES =
AM_CFLAGS = -I@top_srcdir@/src/

libbin_rsyslog_tcp_la_SOURCES = bin_rsyslog_tcp.c
libbin_rsyslog_tcp_la_CFLAGS = $(AM_CFLAGS) @EVENTINC@
libbin_rsyslog_tcp_la_LIBADD = @EVENTLIBS@ \
			       -lpthread \
			       ../baler/libbaler.la
lib_LTLIBRARIES += libbin_rsyslog_tcp.la

libbin_metric_la_SOURCES = bin_metric.c
libbin_metric_la_CFLAGS = $(AM_CFLAGS) @EVENTINC@
libbin_metric_la_LIBADD = @EVENTLIBS@ \
			  -lpthread \
			  ../baler/libbaler.la
lib_LTLIBRARIES += libbin_metric.la

libbout_sos_la_SOURCES = bout_sos.c
libbout_sos_la_CFLAGS = $(AM_CFLAGS) @SOS_INCDIR_FLAG@
libbout_sos_la_LIBADD = ../baler/libbutils.la -lsos ../baler/libbaler.la \
			-lpthread \
			-lobjidx \
			-lods \
			@SOS_LIBDIR_FLAG@
lib_LTLIBRARIES += libbout_sos.la

libbout_sos_img_la_SOURCES = bout_sos_img.c
libbout_sos_img_la_CFLAGS = $(AM_CFLAGS) @SOS_INCDIR_FLAG@
libbout_sos_img_la_LIBADD = ../baler/libbutils.la ../baler/libbaler.la \
			    libbout_sos.la \
			    -lpthread \
			    -lobjidx \
			    -lods \
			    -lsos @SOS_LIBDIR_FLAG@
lib_LTLIBRARIES += libbout_sos_img.la

libbout_sos_msg_la_SOURCES = bout_sos_msg.c
libbout_sos_msg_la_CFLAGS = $(AM_CFLAGS) @SOS_INCDIR_FLAG@
libbout_sos_msg_la_LIBADD = ../baler/libbutils.la ../baler/libbaler.la \
			    libbout_sos.la \
			    -lpthread \
			    -lobjidx \
			    -lods \
			    -lsos @SOS_LIBDIR_FLAG@
lib_LTLIBRARIES += libbout_sos_msg.la

libbout_me_la_SOURCES = bout_me.c
libbout_me_la_CFLAGS = $(AM_CFLAGS) @OVIS_LIB_INCDIR_FLAG@ @EVENTINC@
libbout_me_la_LIBADD = ../baler/libbutils.la ../baler/libbaler.la \
		       -lpthread -lzap @EVENTLIBS@ @OVIS_LIB_LIBDIR_FLAG@
lib_LTLIBRARIES += libbout_me.la


include $(top_srcdir)/ldms/rules.mk


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@

if ENABLE_HELLO_LDMS_MSG

libhello_ldms_msg_la_SOURCES = hello_ldms_msg.c
libhello_ldms_msg_la_LIBADD = $(COMMON_LIBADD)

pkglib_LTLIBRARIES += libhello_ldms_msg.la
dist_man7_MANS += ldms-sampler_hello_ldms_msg.man

endif

CLEANFILES = $(dist_man7_MANS)

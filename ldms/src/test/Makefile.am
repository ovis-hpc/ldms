include $(top_srcdir)/ldms/rules.mk


ACLOCAL_AMFLAGS = -I m4
AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll
dist_man8_MANS =

sbin_PROGRAMS =
sbin_PROGRAMS += test_ldms_xprt_reconnect
test_ldms_xprt_reconnect_SOURCES = test_ldms_xprt_reconnect.c
test_ldms_xprt_reconnect_LDADD = -lldms
test_ldms_xprt_reconnect_LDFLAGS = $(AM_LDFLAGS) -lpthread

sbin_PROGRAMS += test_ldms_notify
test_ldms_notify_SOURCES = test_ldms_notify.c
test_ldms_notify_LDADD = -lldms
test_ldms_notify_LDFLAGS = $(AM_LDFLAGS) -pthread

sbin_PROGRAMS += test_ldms_xprt_send_recv
test_ldms_xprt_send_recv_SOURCES = test_ldms_xprt_send_recv.c
test_ldms_xprt_send_recv_LDADD = -lldms
test_ldms_xprt_send_recv_LDFLAGS = $(AM_LDFLAGS) -pthread

sbin_PROGRAMS += test_ldms_push
test_ldms_push_SOURCES = test_ldms_push.c
test_ldms_push_LDADD = -lldms
test_ldms_push_LDFLAGS = $(AM_LDFLAGS) -pthread

sbin_PROGRAMS += test_ldms
test_ldms_SOURCES = test_ldms.c
test_ldms_LDADD = -lldms -lovis_util
test_ldms_LDFLAGS = $(AM_LDFLAGS) -pthread

sbin_PROGRAMS += test_ldms_set_info
test_ldms_set_info_SOURCES = test_ldms_set_info.c
test_ldms_set_info_LDADD = -lldms
test_ldms_set_info_LDFLAGS = $(AM_LDFLAGS) -pthread

sbin_PROGRAMS += test_ldms_record
test_ldms_record_SOURCES = test_ldms_record.c
test_ldms_record_LDADD = -lldms

sbin_PROGRAMS += test_ldms_mval_setter_getter
test_ldms_mval_setter_getter_SOURCES = test_ldms_mval_setter_getter.c
test_ldms_mval_setter_getter_LDADD = -lldms

sbin_PROGRAMS += test_ldms_set_new
test_ldms_set_new_SOURCES = test_ldms_set_new.c
test_ldms_set_new_LDADD = -lldms

check_PROGRAMS = test_metric
test_metric_SOURCES = test_metric.c
test_metric_LDADD = -lldms
test_metric_LDFLAGS = $(AM_LDFLAGS) -pthread -lm

# override pkglib sanity checks
mypkglibdir = $(pkglibdir)
mypkglib_SCRIPTS = ldms-run-static-tests.test
mypkglib_DATA = ldms-static-test-list.sh ldms-static-test-bypass
dist_man8_MANS += ldms-run-static-tests.man
# invoke tap-driver
TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
                  $(top_srcdir)/config/tap-driver.sh

installcheck-local:
	./ldms-run-static-tests.test

TESTS = $(check_PROGRAMS) ldms-run-static-tests.test

CLEANFILES = $(dist_man8_MANS)

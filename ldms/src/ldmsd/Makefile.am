EXTRA_DIST=
lib_LTLIBRARIES=
SUBDIRS = . test
sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
check_PROGRAMS = test_plugattr

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

AM_CFLAGS = \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmsdincludedir = $(includedir)/ldms
ldmsdinclude_HEADERS = ldmsd.h ldmsd_stream.h

LZAP = $(top_builddir)/lib/src/zap/libzap.la
LMMALLOC = $(top_builddir)/lib/src/mmalloc/libmmalloc.la
LOVIS_UTIL = $(top_builddir)/lib/src/ovis_util/libovis_util.la
LCOLL = $(top_builddir)/lib/src/coll/libcoll.la
LJSON_UTIL = $(top_builddir)/lib/src/ovis_json/libovis_json.la
LOVIS_EVENT = $(top_builddir)/lib/src/ovis_event/libovis_event.la
LOVIS_CTRL = $(top_builddir)/lib/src/ovis_ctrl/libovis_ctrl.la
LOVIS_EV = $(top_builddir)/lib/src/ovis_ev/libovis_ev.la

AM_CFLAGS += -DPLUGINDIR='"$(pkglibdir)"'

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_LDADD = ../core/libldms.la $(LZAP) $(LMMALLOC) $(LOVIS_UTIL) $(LCOLL) \
        -lpthread \
        @LDFLAGS_GETTIME@

lib_LTLIBRARIES += libldmsd_stream.la
libldmsd_stream_la_SOURCES = ldmsd_stream.c ldmsd_stream.h ldmsd_request_util.c
libldmsd_stream_la_LIBADD = ../core/libldms.la



lib_LTLIBRARIES += libldmsd_request.la
libldmsd_request_la_SOURCES = ldmsd_request_util.c
libldmsd_request_la_LIBADD = ../core/libldms.la

ldmsd_SOURCES = ldmsd.c ldmsd_config.c \
	ldmsd_request.c \
	ldmsd_request.h \
	ldmsd_cfgobj.c ldmsd_prdcr.c ldmsd_updtr.c ldmsd_strgp.c \
	ldmsd_failover.c ldmsd_group.c ldmsd_auth.c \
	ldmsd_event.c ldmsd_event.h \
	ldmsd_decomp.c
ldmsd_LDADD = ../core/libldms.la libldmsd_request.la libldmsd_stream.la \
	$(LZAP) $(LMMALLOC) $(LOVIS_UTIL) $(LCOLL) $(LJSON_UTIL) \
	$(LOVIS_EVENT) $(LOVIS_EV) -lpthread $(LOVIS_CTRL) -lm -ldl
ldmsd_CFLAGS = $(AM_CFLAGS)
ldmsd_LDFLAGS = $(AM_LDFLAGS) -rdynamic -pthread

if ENABLE_RABBITV3
ldmsd_LDADD += -lrabbitmq
ldmsd_CFLAGS += -DENABLE_RABBITMQ
endif

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = ../core/libldms.la libldmsd_request.la \
		$(LOVIS_CTRL) $(LOVIS_UTIL) $(LMMALLOC) $(LCOLL) $(LZAP) $(LJSON_UTIL) \
		-lm -lpthread @LDFLAGS_GETTIME@ @LDFLAGS_READLINE@

lib_LTLIBRARIES += libldmsd_plugattr.la
libldmsd_plugattr_la_SOURCES = ldmsd_plugattr.c ldmsd_plugattr.h
libldmsd_plugattr_la_LIBADD = $(LOVIS_UTIL) $(LCOLL)

test_plugattr_SOURCES = ldmsd_plugattr.c ldmsd_plugattr.h
test_plugattr_CFLAGS = -DTEST_PLUGATTR $(AM_CFLAGS)
test_plugattr_LDADD = $(LOVIS_UTIL) $(LCOLL) -lm -lpthread



# make sym links for aggd scripting support
install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsd ldms-aggd
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsctl ldmsaggctl

check-local:
	LD_LIBRARY_PATH=$(DESTDIR)$(libdir) ./test_plugattr $(srcdir)/input/test_plugattr.txt

EXTRA_DIST += $(srcdir)/input/test_plugattr.txt
EXTRA_DIST += $(srcdir)/input/bad_test_plugattr.txt

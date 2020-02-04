EXTRA_DIST=
lib_LTLIBRARIES=
SUBDIRS = . test
sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
check_PROGRAMS = test_plugattr

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

if ENABLE_SOS
sbin_PROGRAMS += ldmsd_sos_init
endif

AM_CFLAGS = \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmsdincludedir = $(includedir)/ldms
ldmsdinclude_HEADERS = ldmsd.h ldmsd_stream.h

AM_CFLAGS += -DPLUGINDIR='"$(pkglibdir)"'

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_LDADD = -lldms -lzap -lmmalloc -lovis_util -lcoll \
        -lpthread \
        @LDFLAGS_GETTIME@

lib_LTLIBRARIES += libldmsd_stream.la
libldmsd_stream_la_SOURCES = ldmsd_stream.c ldmsd_stream.h ldmsd_request_util.c
libldmsd_stream_la_LIBADD = -lldms

lib_LTLIBRARIES += libldmsd_request.la
libldmsd_request_la_SOURCES = ldmsd_request_util.c
libldmsd_request_la_LIBADD = -lldms

ldmsd_SOURCES = ldmsd.c ldmsd_config.c \
	ldmsd_request.c \
	ldmsd_request.h \
	ldmsd_cfgobj.c ldmsd_prdcr.c ldmsd_updtr.c ldmsd_strgp.c \
	ldmsd_failover.c ldmsd_group.c
ldmsd_LDADD = -lldms libldmsd_request.la libldmsd_stream.la \
	-lzap -lmmalloc -lovis_util -lcoll -ljson_util \
	-lovis_event -lpthread -lovis_ctrl -lm -ldl
ldmsd_CFLAGS = $(AM_CFLAGS)
ldmsd_LDFLAGS = $(AM_LDFLAGS) -rdynamic -pthread

if ENABLE_RABBITV3
ldmsd_LDADD += -lrabbitmq
ldmsd_CFLAGS += -DENABLE_RABBITMQ
endif

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = -lldms libldmsd_request.la \
		-lovis_ctrl -lovis_util -lmmalloc -lcoll -lzap -ljson_util \
		-lm -lpthread @LDFLAGS_GETTIME@ @LDFLAGS_READLINE@

ldmsd_sos_init_SOURCES = ldmsd_sos_init.c
ldmsd_sos_init_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
ldmsd_sos_init_LDADD = -lsos -lods -lpthread @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@

lib_LTLIBRARIES += libldmsd_plugattr.la
libldmsd_plugattr_la_SOURCES = ldmsd_plugattr.c ldmsd_plugattr.h
libldmsd_plugattr_la_LIBADD = -lovis_util -lcoll

test_plugattr_SOURCES = ldmsd_plugattr.c ldmsd_plugattr.h
test_plugattr_CFLAGS = -DTEST_PLUGATTR $(AM_CFLAGS)
test_plugattr_LDADD = -lovis_util -lcoll -lm -lpthread

# make sym links for aggd scripting support
install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsd ldms-aggd
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsctl ldmsaggctl

check-local:
	LD_LIBRARY_PATH=$(DESTDIR)$(libdir) ./test_plugattr $(srcdir)/input/test_plugattr.txt
EXTRA_DIST += $(srcdir)/input/test_plugattr.txt
EXTRA_DIST += $(srcdir)/input/bad_test_plugattr.txt

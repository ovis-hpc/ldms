EXTRA_DIST=
lib_LTLIBRARIES=
SUBDIRS = . test
sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
check_PROGRAMS = test_plugattr

if ENABLE_SOS
sbin_PROGRAMS += ldmsd_sos_init
endif

CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I.. @OVIS_LIB_INCDIR_FLAG@
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmsdincludedir = $(includedir)/ldms
ldmsdinclude_HEADERS = ldmsd.h ldmsd_stream.h

AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'

SUBDIRS += test

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_CFLAGS = $(AM_CFLAGS)
ldms_ls_LDADD = $(CORE)/libldms.la
ldms_ls_LDFLAGS = $(AM_LDFLAGS) \
	-lzap -lmmalloc -lovis_util -lcoll \
	-lpthread \
	@LDFLAGS_GETTIME@

lib_LTLIBRARIES += libldmsd_stream.la
libldmsd_stream_la_SOURCES = ldmsd_stream.c ldmsd_stream.h ../core/ldms.h ../core/ldms_core.h ldmsd_request_util.c
libldmsd_stream_la_LIBADD = \
	@OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@ $(CORE)/libldms.la

lib_LTLIBRARIES += libldmsd_request.la
libldmsd_request_la_SOURCES = ../core/ldms.h ../core/ldms_core.h ldmsd_request_util.c
libldmsd_request_la_LIBADD = \
	@OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@ $(CORE)/libldms.la

ldmsd_SOURCES = ldmsd.c ldmsd_config.c \
	ldmsd_request.c \
	ldmsd_request.h \
	ldmsd_cfgobj.c ldmsd_prdcr.c ldmsd_updtr.c ldmsd_strgp.c \
	ldmsd_failover.c ldmsd_group.c
ldmsd_CFLAGS = $(AM_CFLAGS) -rdynamic
ldmsd_LDADD = $(CORE)/libldms.la libldmsd_request.la libldmsd_stream.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) \
	-lzap -lmmalloc -lovis_util -lcoll -ljson_util \
	-lovis_event -lpthread -lm

ldmsd_LDFLAGS += -lovis_util -lcoll -ldl -lovis_ctrl

if ENABLE_RABBITV3
ldmsd_LDFLAGS += -lrabbitmq
ldmsd_CFLAGS += -DENABLE_RABBITMQ
endif

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = $(CORE)/libldms.la libldmsd_request.la
ldmsctl_LDFLAGS = $(AM_LDFLAGS) @LDFLAGS_READLINE@ \
		-lovis_ctrl -lovis_util -lmmalloc -lcoll -lzap -ljson_util \
		 -ljson_parser -lm -pthread @LDFLAGS_GETTIME@

ldmsd_sos_init_SOURCES = ldmsd_sos_init.c
ldmsd_sos_init_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
ldmsd_sos_init_LDADD = -lsos -lods -lpthread @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@
ldmsd_sos_init_LDFLAGS = $(AM_LDFLAGS)

lib_LTLIBRARIES += libplugattr.la
libplugattr_la_SOURCES = ldmsd_plugattr.c ldmsd_plugattr.h
libplugattr_la_LIBADD = -lovis_util -lcoll

test_plugattr_SOURCES = ldmsd_plugattr.c ldmsd_plugattr.h
test_plugattr_CFLAGS = -DTEST_PLUGATTR $(AM_CFLAGS)
test_plugattr_LDADD = -lovis_util -lcoll -lm -lpthread
test_plugattr_LDFLAGS = $(AM_LDFLAGS)

# make sym links for aggd scripting support
install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsd ldms-aggd
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsctl ldmsaggctl

check-local:
	LD_LIBRARY_PATH=$(DESTDIR)$(libdir) ./test_plugattr $(srcdir)/input/test_plugattr.txt
EXTRA_DIST += $(srcdir)/input/test_plugattr.txt
EXTRA_DIST += $(srcdir)/input/bad_test_plugattr.txt

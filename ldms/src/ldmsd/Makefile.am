sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
if ENABLE_SOS
sbin_PROGRAMS += ldmsd_sos_init
endif
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I.. @OVIS_LIB_INCDIR_FLAG@
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmsdincludedir = $(includedir)/ldms
ldmsdinclude_HEADERS = ldmsd.h

AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_CFLAGS = $(AM_CFLAGS) @LIBEVENT_INCDIR_FLAG@
ldms_ls_LDADD = $(CORE)/libldms.la
ldms_ls_LDFLAGS = $(AM_LDFLAGS) \
	@LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
	-lzap -lmmalloc -lovis_util -lcoll \
	-levent -levent_pthreads -lpthread \
	@LDFLAGS_GETTIME@

ldmsd_SOURCES = ldmsd.c ldmsd_config.c ldmsd_request.c ldmsd_request_util.c \
	ldmsd_request.h \
	ldmsd_cfgobj.c ldmsd_prdcr.c ldmsd_updtr.c ldmsd_strgp.c
ldmsd_CFLAGS = $(AM_CFLAGS) @LIBEVENT_INCDIR_FLAG@
ldmsd_LDADD = $(CORE)/libldms.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) \
	@LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
	-lzap -lmmalloc -lovis_util -lcoll \
	-levent -levent_pthreads -lpthread

ldmsd_LDFLAGS += -lovis_util -lcoll -ldl -lovis_ctrl

if ENABLE_RABBITV3
ldmsd_LDFLAGS += -lrabbitmq
ldmsd_CFLAGS += -DENABLE_RABBITMQ
endif

ldmsd_LDFLAGS += @OVIS_AUTH_LIBS@
ldms_ls_LDADD += @OVIS_AUTH_LIBS@

ldmsctl_SOURCES = ldmsctl.c ldmsd_request_util.c
ldmsctl_LDADD = $(CORE)/libldms.la @OVIS_AUTH_LIBS@
ldmsctl_LDFLAGS = $(AM_LDFLAGS) @LDFLAGS_READLINE@ \
		-lovis_ctrl -lovis_util -lmmalloc -lcoll -lzap -pthread \
		 -ljson_parser -lm @LDFLAGS_GETTIME@

ldmsd_sos_init_SOURCES = ldmsd_sos_init.c
ldmsd_sos_init_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
ldmsd_sos_init_LDADD = -lsos -lods -lpthread @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@
ldmsd_sos_init_LDFLAGS = $(AM_LDFLAGS)

# make sym links for aggd scripting support
install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsd ldms-aggd
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsctl ldmsaggctl

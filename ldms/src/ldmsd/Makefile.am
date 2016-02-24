sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
if ENABLE_SOS
sbin_PROGRAMS += ldmsd_sos_init
endif
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_CFLAGS = $(AM_CFLAGS) @LIBEVENT_INCDIR_FLAG@
ldms_ls_LDADD = $(CORE)/libldms.la
ldms_ls_LDFLAGS = $(AM_LDFLAGS) \
	@LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
	-lzap -lmmalloc -lovis_util -lcoll -lovis_third \
	-levent -levent_pthreads -lpthread \
	@LDFLAGS_GETTIME@
#	@LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
#	-lzap -lmmalloc -lovis_util -lovis_auth -lcoll -lovis_third \
#	-levent -levent_pthreads -lpthread \
#	@LDFLAGS_GETTIME@

ldmsd_SOURCES = ldmsd.c ldmsd_store.c ldmsd_config.c ldmsd_request.c \
	ldmsd_cfgobj.c ldmsd_prdcr.c ldmsd_updtr.c ldmsd_strgp.c
ldmsd_CFLAGS = $(AM_CFLAGS) @LIBEVENT_INCDIR_FLAG@
ldmsd_LDADD = $(CORE)/libldms.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) \
	@LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
	-lzap -lmmalloc -lovis_util -lcoll -lovis_third \
	-levent -levent_pthreads -lpthread

if ENABLE_OCM
ldmsd_SOURCES += ldmsd_ocm.c
ldmsd_LDFLAGS += @OCM_LIBDIR_FLAG@ -locm
ldmsd_CFLAGS += @OCM_INCDIR_FLAG@
endif

ldmsd_LDFLAGS += -lovis_util -lcoll -lovis_third -ldl -lovis_ctrl

if ENABLE_RABBITV3
ldmsd_LDFLAGS += -lrabbitmq
ldmsd_CFLAGS += -DENABLE_RABBITMQ
endif

ldmsd_LDFLAGS += @OVIS_AUTH_LIBS@
ldms_ls_LDADD += @OVIS_AUTH_LIBS@

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = $(CORE)/libldms.la @OVIS_AUTH_LIBS@
ldmsctl_LDFLAGS = $(AM_LDFLAGS) @LDFLAGS_READLINE@ -lovis_ctrl -lovis_util -lmmalloc -lcoll -lovis_third -lzap  @LDFLAGS_GETTIME@

if ENABLE_LDMSD_RCTL
ldmsd_SOURCES += ldmsd_config_rctrl.c
ldmsd_LDFLAGS += -locm -lovis_rctrl @OCM_LIBDIR_FLAG@
ldmsd_CFLAGS += -DENABLE_LDMSD_RCTL  @OCM_INCDIR_FLAG@

sbin_PROGRAMS += ldmsd_rctl
ldmsd_rctl_SOURCES = ldmsd_rctl.c
ldmsd_rctl_LDADD = $(CORE)/libldms.la
ldmsd_rctl_LDFLAGS = $(AM_LDFLAGS) -lovis_rctrl -lzap -lmmalloc -locm -lpthread \
		      @OCM_LIBDIR_FLAG@ @LDFLAGS_READLINE@
ldmsd_rctl_CFLAGS = $(AM_CFLAGS) @OCM_INCDIR_FLAG@
ldmsd_rctl_LDADD += @OVIS_AUTH_LIBS@
endif

ldmsd_sos_init_SOURCES = ldmsd_sos_init.c
ldmsd_sos_init_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
ldmsd_sos_init_LDADD = -lsos -lpthread @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@
ldmsd_sos_init_LDFLAGS = $(AM_LDFLAGS)

# make sym links for aggd scripting support
install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsd ldms-aggd
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsctl ldmsaggctl

sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
if ENABLE_SOS
sbin_PROGRAMS += ldmsd_sos_init
endif
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_LDADD = $(CORE)/libldms.la -lrt -lovis_util -lpthread
ldms_ls_CFLAGS = $(AM_CFLAGS)

ldmsd_SOURCES = ldmsd.c ldmsd_store.c ldmsd_config.c
ldmsd_LDADD = $(CORE)/libldms.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) @EVENTLIBS@ \
		-levent -lpthread -levent_pthreads -lovis_util -lcoll -ldl \
		-lovis_ctrl
ldmsd_CFLAGS = $(AM_CFLAGS) @EVENTINC@

if ENABLE_AUTH
ldmsd_CFLAGS += -DENABLE_AUTH
ldmsd_LDFLAGS += -lovis_auth

ldms_ls_CFLAGS += -DENABLE_AUTH
ldms_ls_LDADD += -lovis_auth
endif

if ENABLE_LDMSD_TEST
ldmsd_CFLAGS += -DENABLE_LDMSD_TEST
endif

if ENABLE_OCM
ldmsd_SOURCES += ldmsd_ocm.c
ldmsd_LDFLAGS += -lcoll -locm @OCM_LIBDIR_FLAG@
ldmsd_CFLAGS += -DENABLE_OCM @OCM_INCDIR_FLAG@
endif

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = $(CORE)/libldms.la
ldmsctl_LDFLAGS = $(AM_LDFLAGS) -lreadline -lovis_ctrl -lovis_util \
		  @LDFLAGS_READLINE@

if ENABLE_LDMSD_RCTRL
ldmsd_SOURCES += ldmsd_config_rctrl.c
ldmsd_LDFLAGS += -locm -lovis_rctrl @OCM_LIBDIR_FLAG@
ldmsd_CFLAGS += -DENABLE_LDMSD_RCTRL  @OCM_INCDIR_FLAG@

sbin_PROGRAMS += ldmsd_rctl
ldmsd_rctl_SOURCES = ldmsd_rctl.c
ldmsd_rctl_LDADD = $(CORE)/libldms.la
ldmsd_rctl_LDFLAGS = $(AM_LDFLAGS) -lreadline -lovis_rctrl -locm -lpthread \
		      @OCM_LIBDIR_FLAG@ @LDFLAGS_READLINE@
ldmsd_rctl_CFLAGS = $(AM_CFLAGS) @OCM_INCDIR_FLAG@
if ENABLE_AUTH
ldmsd_rctl_CFLAGS += -DENABLE_AUTH
ldmsd_rctl_LDADD += -lovis_auth
endif
endif

ldmsd_sos_init_SOURCES = ldmsd_sos_init.c
ldmsd_sos_init_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
ldmsd_sos_init_LDADD = -lsos -lpthread @SOS_LIBDIR_FLAG@
ldmsd_sos_init_LDFLAGS = $(AM_LDFLAGS)

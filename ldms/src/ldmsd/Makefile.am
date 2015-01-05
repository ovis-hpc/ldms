sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
if ENABLE_SOS
sbin_PROGRAMS += ldmsd_sos_init
endif
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_LDADD = $(CORE)/libldms.la -lrt -lovis_util

ldmsd_SOURCES = ldmsd.c ldmsd_store.c
ldmsd_LDADD = $(CORE)/libldms.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) @EVENTLIBS@ \
		-levent -lpthread -levent_pthreads -lovis_util -ldl
ldmsd_CFLAGS = $(AM_CFLAGS) @EVENTINC@
if ENABLE_YAML
ldmsd_LDFLAGS += -lyaml
ldmsd_CFLAGS += -DENABLE_YAML
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

ldmsd_sos_init_SOURCES = ldmsd_sos_init.c
ldmsd_sos_init_LDADD = -lsos -lpthread
ldmsd_sos_init_LDFLAGS = $(AM_LDFLAGS)

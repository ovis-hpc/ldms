sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE)

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_LDADD = $(CORE)/libldms.la -lrt -lovis_util

ldmsd_SOURCES = ldmsd.c ldmsd_store.c
ldmsd_LDADD = $(CORE)/libldms.la
ldmsd_LDFLAGS = -levent -lpthread -levent_pthreads -lovis_util
ldmsd_CFLAGS = $(AM_CFLAGS)
if ENABLE_YAML
ldmsd_LDFLAGS += -lyaml
ldmsd_CFLAGS += -DENABLE_YAML
endif

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = $(CORE)/libldms.la
ldmsctl_LDFLAGS = -lreadline -ltermcap -lovis_ctrl -lovis_util

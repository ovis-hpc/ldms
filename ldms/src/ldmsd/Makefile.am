sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I@OVIS_LIB_INCDIR@
AM_LDFLAGS = -L@OVIS_LIB_LIBDIR@

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_LDADD = $(CORE)/libldms.la -lrt -lovis_util

ldmsd_SOURCES = ldmsd.c ldmsd_store.c
ldmsd_LDADD = $(CORE)/libldms.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) -L@LIBEVENT_LIBDIR@ -levent -lpthread \
		-levent_pthreads -lovis_util -ldl
ldmsd_CFLAGS = $(AM_CFLAGS) -I@LIBEVENT_INCDIR@
if ENABLE_YAML
ldmsd_LDFLAGS += -lyaml
ldmsd_CFLAGS += -DENABLE_YAML
endif

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = $(CORE)/libldms.la
ldmsctl_LDFLAGS = $(AM_LDFLAGS) -lreadline -lovis_ctrl -lovis_util

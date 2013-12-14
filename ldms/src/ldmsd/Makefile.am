sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_LDADD = $(CORE)/libldms.la -lrt -lovis_util -lmmalloc -lcoll

ldmsd_SOURCES = ldmsd.c ldmsd_store.c
ldmsd_LDADD = $(CORE)/libldms.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) @LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
		-levent -lpthread -levent_pthreads -lovis_util -lmmalloc -lcoll -ldl
ldmsd_CFLAGS = $(AM_CFLAGS) @LIBEVENT_INCDIR_FLAG@

if ENABLE_YAML
ldmsd_LDFLAGS += -lyaml
ldmsd_CFLAGS += -DENABLE_YAML
endif

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = $(CORE)/libldms.la
ldmsctl_LDFLAGS = $(AM_LDFLAGS) @LDFLAGS_READLINE@ -lovis_ctrl -lovis_util  -lmmalloc -lcoll

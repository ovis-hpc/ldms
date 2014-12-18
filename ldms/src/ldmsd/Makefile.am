sbin_PROGRAMS = ldmsd ldms_ls ldmsctl
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'

if ENABLE_AUTHENTICATION
SSL_LDFLAGS=@OPENSSL_LDFLAGS@
SSL_LIBS=@OPENSSL_LIBS@
SSL_INCLUDES=@OPENSSL_INCLUDES@
endif


ldms_ls_SOURCES = ldms_ls.c
ldms_ls_LDADD = $(CORE)/libldms.la -lrt -lovis_util -lmmalloc -lcoll \
		$(SSL_LDFLAGS) $(SSL_LIBS)

ldmsd_SOURCES = ldmsd.c ldmsd_store.c
ldmsd_LDADD = $(CORE)/libldms.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) @LIBEVENT_LIB64DIR_FLAG@ @LIBEVENT_LIBDIR_FLAG@ \
		$(SSL_LDFLAGS) \
		-levent -lpthread -levent_pthreads \
		-lovis_util -lmmalloc -lcoll \
		$(SSL_LIBS) \
		-ldl
ldmsd_CFLAGS = $(AM_CFLAGS) @LIBEVENT_INCDIR_FLAG@ $(SSL_INCLUDES)

if ENABLE_YAML
ldmsd_LDFLAGS += -lyaml
ldmsd_CFLAGS += -DENABLE_YAML
endif

ldmsctl_SOURCES = ldmsctl.c
ldmsctl_LDADD = $(CORE)/libldms.la
ldmsctl_LDFLAGS = $(AM_LDFLAGS) @LDFLAGS_READLINE@ -lovis_ctrl -lovis_util  -lmmalloc -lcoll

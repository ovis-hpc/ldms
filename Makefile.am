
ACLOCAL_AMFLAGS = -I m4

INCLUDES = -I$(top_builddir)/include/ -I$(srcdir)/include -I$(includedir) \
	-I$(includedir)/infiniband 

sbin_PROGRAMS = src/ldmsd src/ldms_ls src/ldmsctl src/sedc_to_ldms

noinst_LIBRARIES = libun.a

libun_a_SOURCES = src/libun.c
libun_a_DEPENDENCIES = src/un.h

if HAVE_SPINLOCK_T
SPINLOCK_FLAGS = -DHAVE_SPINLOCK_T
endif

if ENABLE_RDMA
src_libldmsrdma_la_SOURCES = src/ldms_rdma.c
src_libldmsrdma_la_LIBADD = -libverbs -lrdmacm
src_libldmsrdma_la_DEPENDENCIES = $(srcdir)/src/libldmsrdma.map
RDMA_la = src/libldmsrdma.la
else
RDMA_FLAGS = -DDISABLE_RDMA
endif

if ENABLE_MMAP
MMAP_FLAGS = -DENABLE_MMAP
endif

if ENABLE_SOCK
src_libldmssock_la_SOURCES = src/ldms_sock.c
src_libldmssock_la_DEPENDENCIES = $(srcdir)/src/libldmssock.map
SOCK_la = src/libldmssock.la
else
SOCK_FLAGS = -DDISABLE_SOCK
endif

src_libflatfile_la_SOURCES = src/flatfile.c
src_libflatfile_la_DEPENDENCIES = $(srcdir)/src/libflatfile.map
src_libflatfile_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
FLATFILE_la = src/libflatfile.la

src_libmysqlinsert_la_CPPFLAGS = -I/usr/include/mysql 
src_libmysqlinsert_la_SOURCES = src/mysqlinsert.c
src_libmysqlinsert_la_DEPENDENCIES = $(srcdir)/src/libmysqlinsert.map
src_libmysqlinsert_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir) -L/usr/lib64/mysql -lmysqlclient 
MYSQLINSERT_la = src/libmysqlinsert.la

src_libmeminfo_la_SOURCES = src/meminfo.c
src_libmeminfo_la_DEPENDENCIES = $(srcdir)/src/libmeminfo.map
src_libmeminfo_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
MEMINFO_la = src/libmeminfo.la

src_libgeminfo_la_SOURCES = src/geminfo.c
src_libgeminfo_la_DEPENDENCIES = $(srcdir)/src/libgeminfo.map
src_libgeminfo_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
GEMINFO_la = src/libgeminfo.la

src_libprocinterrupts_la_SOURCES = src/procinterrupts.c
src_libprocinterrupts_la_DEPENDENCIES = $(srcdir)/src/libprocinterrupts.map
src_libprocinterrupts_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
PROCINTERRUPTS_la = src/libprocinterrupts.la

src_libprocstatutil_la_SOURCES = src/procstatutil.c
src_libprocstatutil_la_DEPENDENCIES = $(srcdir)/src/libprocstatutil.map
src_libprocstatutil_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
PROCSTATUTIL_la = src/libprocstatutil.la

src_libvmstat_la_SOURCES = src/vmstat.c
src_libvmstat_la_DEPENDENCIES = $(srcdir)/src/libvmstat.map
src_libvmstat_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
VMSTAT_la = src/libvmstat.la

src_libperfevent_la_SOURCES = src/perfevent.c
src_libperfevent_la_DEPENDENCIES = $(srcdir)/src/libperfevent.map
src_libperfevent_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)  -lm
PERFEVENT_la = src/libperfevent.la

AM_CFLAGS = -Wall $(DBGFLAGS) $(SOCK_FLAGS) $(RDMA_FLAGS) $(MMAP_FLAGS) $(LOCAL_FLAGS) ${SPINLOCK_FLAGS}

lib_LTLIBRARIES = src/libldms.la $(SOCK_la) \
	$(RDMA_la) $(FLATFILE_la) $(MYSQLINSERT_la) $(MEMINFO_la) $(GEMINFO_la) $(PROCINTERRUPTS_la) \
	$(VMSTAT_la) $(PROCSTATUTIL_la) $(PERFEVENT_la)

src_libldms_la_SOURCES = src/ldms.c src/ldms_sock.c src/ldms_xprt.c \
		src/ogc_rbt.c
src_libldms_la_LIBADD = -ldl -lpthread -levent -levent_pthreads
src_libldms_la_DEPENDENCIES = $(srcdir)/src/libldms.map

libldmsincludedir = $(includedir)/ldms
libldmsinclude_HEADERS = src/ldms.h src/ldms_sock_xprt.h \
                         src/ldms_xprt.h \
                         src/ogc_rbt.h 

src_ldms_ls_SOURCES = src/ldms_ls.c
src_ldms_ls_LDADD = $(top_builddir)/src/libldms.la

src_ldmsd_SOURCES = src/ldmsd.c src/ldmsd_cfg.c
src_ldmsd_LDADD = $(top_builddir)/src/libldms.la -L$(top_builddir) -lun -levent
src_ldmsd_DEPENDENCIES = $(top_builddir)/libun.a

src_ldmsctl_SOURCES = src/ldmsctl.c
src_ldmsctl_LDFLAGS = $(top_builddir)/src/libldms.la -L$(top_builddir) -lun -lreadline -ltermcap
src_ldmsctl_DEPENDENCIES = $(top_builddir)/libun.a

src_sedc_to_ldms_SOURCES = src/sedc_to_ldms.cxx
src_sedc_to_ldms_LDFLAGS = $(top_builddir)/src/libldms.la -L$(top_builddir) -lun -lreadline -ltermcap
src_sedc_to_ldms_DEPENDENCIES = $(top_builddir)/libun.a

EXTRA_DIST = include ldms-utils.spec.in ldms-utils.spec \
	autogen.sh \
	src/libldms.map \
	src/ldms.h src/ldms_rdma_xprt.h src/ldms_xprt.h src/ogc_rbt.h \
	src/un.h

dist-hook: ldms-utils.spec
	cp ldms-utils.spec $(distdir)

install-data-hook:
#	cp $(top_builddir)/scripts/ldmsd /etc/init.d/ldmsd
#	cp $(top_builddir)/scripts/schedstatd /etc/init.d/schedstatd
#	cp $(top_builddir)/scripts/meminfod /etc/init.d/meminfod
#	cp $(top_builddir)/scripts/procstatutild /etc/init.d/procstatutild


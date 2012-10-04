ACLOCAL_AMFLAGS = -I m4

INCLUDES = -I$(top_builddir)/include/ -I$(srcdir)/include -I$(includedir)

if ENABLE_GLIB
sbin_PROGRAMS = src/ldmsd src/ldms_ls src/ldmsctl src/hwlocldms_ls
else
sbin_PROGRAMS = src/ldmsd src/ldms_ls src/ldmsctl
endif

if ENABLE_RDMA
src_libldmsrdma_la_SOURCES = src/ldms_rdma.c
src_libldmsrdma_la_LIBADD = -libverbs -lrdmacm
src_libldmsrdma_la_DEPENDENCIES = $(srcdir)/src/libldmsrdma.map
src_libldmsrdma_la_CFLAGS = -Iinfiniband
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

if ENABLE_UGNI
PMI_CFLAGS = $(shell pkg-config --cflags cray-pmi)
PMI_LIBS = $(shell pkg-config --libs cray-pmi)
UGNI_CFLAGS = $(shell pkg-config --cflags cray-ugni)
UGNI_LIBS = $(shell pkg-config --libs cray-ugni)
src_libldmsugni_la_CFLAGS = $(PMI_CFLAGS) $(UGNI_CFLAGS)
src_libldmsugni_la_LDFLAGS = $(PMI_LIBS) $(UGNI_LIBS)
src_libldmsugni_la_SOURCES = src/ldms_ugni.c
src_libldmsugni_la_DEPENDENCIES = $(srcdir)/src/libldmsugni.map
UGNI_la = src/libldmsugni.la
endif

src_libflatfile_la_SOURCES = src/flatfile.c
src_libflatfile_la_DEPENDENCIES = $(srcdir)/src/libflatfile.map
src_libflatfile_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
FLATFILE_la = src/libflatfile.la

#src_libhwlocflatfile_la_SOURCES = src/hwlocflatfile.c
#src_libhwlocflatfile_la_DEPENDENCIES = $(srcdir)/src/libhwlocflatfile.map
#src_libhwlocflatfile_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir) $(top_builddir)/src/libmetricmap.la
#HWLOCFLATFILE_la = src/libhwlocflatfile.la

if ENABLE_MYSQL
src_libmysqlinsert_la_CPPFLAGS = -I/usr/include/mysql
src_libmysqlinsert_la_SOURCES = src/mysqlinsert.c
src_libmysqlinsert_la_DEPENDENCIES = $(srcdir)/src/libmysqlinsert.map
src_libmysqlinsert_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir) -L/usr/lib64/mysql -lmysqlclient
MYSQLINSERT_la = src/libmysqlinsert.la
else
MYSQL_FLAGS = -DDISABLE_MYSQL
endif

if ENABLE_GLIB
src_libsedc_la_SOURCES = src/sedc.c
src_libsedc_la_DEPENDENCIES = $(srcdir)/src/libsedc.map
src_libsedc_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir) \
	$(GLIB20_LIBS)
src_libsedc_la_CFLAGS = $(GLIB20_CFLAGS)
SEDC_la = src/libsedc.la

src_libmetricmap_la_SOURCES = src/metricmap.c src/metricmap.h
src_libmetricmap_la_LIBADD = -ldl -lpthread -levent -levent_pthreads $(GLIB20_LIBS)
src_libmetricmap_la_DEPENDENCIES = $(srcdir)/src/libmetricmap.map
src_libmetricmap_la_CFLAGS = $(GLIB20_CFLAGS)

src_hwlocldms_ls_SOURCES = src/hwlocldms_ls.c
src_hwlocldms_ls_LDADD = $(top_builddir)/src/libldms.la \
	$(top_builddir)/src/libmetricmap.la \
	$(GLIB20_LIBS)
src_hwlocldms_ls_CFLAGS = $(GLIB20_CFLAGS)

else
GLIB_FLAGS = -DISABLE_GLIB
endif

if ENABLE_SOS
src_libstore_sos_la_SOURCES = src/store_sos.c
src_libstore_sos_la_DEPENDENCIES = $(srcdir)/src/libstore_sos.map
src_libstore_sos_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir) -lsos -lplugin
STORE_SOS_la = src/libstore_sos.la
endif

src_libgeminfo_la_SOURCES = src/geminfo.c
src_libgeminfo_la_DEPENDENCIES = $(srcdir)/src/libgeminfo.map
src_libgeminfo_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
GEMINFO_la = src/libgeminfo.la

src_liblmsensors_la_SOURCES = src/lmsensors.c
src_liblmsensors_la_DEPENDENCIES = $(srcdir)/src/liblmsensors.map
src_liblmsensors_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
LMSENSORS_la = src/liblmsensors.la

src_libmeminfo_la_SOURCES = src/meminfo.c
src_libmeminfo_la_DEPENDENCIES = $(srcdir)/src/libmeminfo.map $(top_builddir)/libplugin.a
src_libmeminfo_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir) -lplugin

MEMINFO_la = src/libmeminfo.la

src_libprocinterrupts_la_SOURCES = src/procinterrupts.c
src_libprocinterrupts_la_DEPENDENCIES = $(srcdir)/src/libprocinterrupts.map
src_libprocinterrupts_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)
PROCINTERRUPTS_la = src/libprocinterrupts.la

src_libprocstatutil_la_SOURCES = src/procstatutil.c $(top_builddir)/libplugin.a
src_libprocstatutil_la_DEPENDENCIES = $(srcdir)/src/libprocstatutil.map
src_libprocstatutil_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir) -lplugin
PROCSTATUTIL_la = src/libprocstatutil.la

src_libvmstat_la_SOURCES = src/vmstat.c
src_libvmstat_la_DEPENDENCIES = $(srcdir)/src/libvmstat.map $(top_builddir)/libplugin.a
src_libvmstat_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir) -lplugin
VMSTAT_la = src/libvmstat.la

if ENABLE_PERF
src_libperfevent_la_SOURCES = src/perfevent.c
src_libperfevent_la_DEPENDENCIES = $(srcdir)/src/libperfevent.map
src_libperfevent_la_LIBADD = $(top_builddir)/src/libldms.la -L$(top_builddir)  -lm
PERFEVENT_la = src/libperfevent.la
endif

AM_CFLAGS = -Wall $(DBGFLAGS) $(SOCK_FLAGS) $(RDMA_FLAGS) $(MMAP_FLAGS) \
	$(MYSQL_FLAGS) $(GLIB_FLAGS) $(LOCAL_FLAGS) ${SPINLOCK_FLAGS}

lib_LTLIBRARIES = src/libldms.la $(SOCK_la) src/libmetricmap.la \
	$(UGNI_la) $(RDMA_la) $(FLATFILE_la) $(MYSQLINSERT_la) $(LMSENSORS_la) \
	$(MEMINFO_la) $(GEMINFO_la) $(PROCINTERRUPTS_la) \
	$(VMSTAT_la) $(PROCSTATUTIL_la) $(PERFEVENT_la) $(SEDC_la) \
	$(STORE_SOS_la)
#$(HWLOCFLATFILE_la)


src_libldms_la_SOURCES = src/ldms.c src/ldms_sock.c src/ldms_xprt.c \
		src/ogc_rbt.c
src_libldms_la_LIBADD = -ldl -lpthread -levent -levent_pthreads
src_libldms_la_DEPENDENCIES = $(srcdir)/src/libldms.map

libldmsincludedir = $(includedir)/ldms
libldmsinclude_HEADERS = src/ldms.h src/ldms_sock_xprt.h \
			 src/ldms_xprt.h \
			 src/ogc_rbt.h

noinst_LIBRARIES = libplugin.a
libplugin_a_CFLAGS = -fPIC
libplugin_a_SOURCES = src/ldmsd_ctrl.c
libplugin_a_DEPENDENCIES = src/ldmsd.h

src_ldms_ls_SOURCES = src/ldms_ls.c
src_ldms_ls_LDADD = $(top_builddir)/src/libldms.la

src_ldmsd_SOURCES = src/ldmsd.c src/ldmsd_store.c
src_ldmsd_LDADD = $(top_builddir)/src/libldms.la -L$(top_builddir) \
	-levent -lplugin
src_ldmsd_DEPENDENCIES = $(top_builddir)/libplugin.a

src_ldmsctl_SOURCES = src/ldmsctl.c
src_ldmsctl_LDFLAGS = $(top_builddir)/src/libldms.la -L$(top_builddir) \
	-lplugin -lreadline -ltermcap
src_ldmsctl_DEPENDENCIES = $(top_builddir)/libplugin.a

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

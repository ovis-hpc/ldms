sbin_PROGRAMS = ldmsd ldms_ls # ldmsctl
check_PROGRAMS = test_plugattr
lib_LTLIBRARIES =
SUBDIRS = . test
if ENABLE_SOS
sbin_PROGRAMS += ldmsd_sos_init
endif
pkglib_LTLIBRARIES =
CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) -I.. -I$(top_srcdir)/lib/src
AM_CFLAGS += \
"-DLDMS_SRCDIR=\"$(abs_top_srcdir)\"" \
"-DLDMS_BUILDDIR=\"$(abs_top_builddir)\""

ldmsdincludedir = $(includedir)/ldms
ldmsdinclude_HEADERS = ldmsd.h ldmsd_stream.h ldmsd_plugin.h

AM_LDFLAGS = -pthread
AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'

ldms_ls_SOURCES = ldms_ls.c
ldms_ls_CFLAGS = $(AM_CFLAGS)
ldms_ls_LDADD = $(CORE)/libldms.la
ldms_ls_LDFLAGS = $(AM_LDFLAGS) \
		  $(top_builddir)/lib/src/zap/libzap.la \
		  $(top_builddir)/lib/src/mmalloc/libmmalloc.la \
		  $(top_builddir)/lib/src/ovis_util/libovis_util.la \
		  $(top_builddir)/lib/src/coll/libcoll.la \
		  -lpthread \
		  @LDFLAGS_GETTIME@

sbin_PROGRAMS += ldmsd_upload
ldmsd_upload_SOURCES = ldmsd_upload.c
ldmsd_upload_CFLAGS = $(AM_CFLAGS) -DLDMSD_UPLOAD_CMD
ldmsd_upload_LDADD = $(CORE)/libldms.la librequest.la libldmsd_stream.la
ldmsd_upload_LDFLAGS = $(AM_LDFLAGS) -pthread \
		       $(top_builddir)/lib/src/ovis_util/libovis_util.la

lib_LTLIBRARIES += libldmsd_stream.la
libldmsd_stream_la_SOURCES = ldmsd_stream.c ldmsd_stream.h \
			     ../core/ldms.h ../core/ldms_core.h \
			     ldmsd_request_util.c
libldmsd_stream_la_LIBADD = $(CORE)/libldms.la

lib_LTLIBRARIES += librequest.la
librequest_la_SOURCES = ../core/ldms.h ../core/ldms_core.h ldmsd_request_util.c
librequest_la_LIBADD = $(CORE)/libldms.la

ldmsd_SOURCES = ldmsd.c ldmsd_config.c \
	ldmsd_request.c \
	ldmsd_request.h \
	ldmsd_cfgobj.c ldmsd_prdcr.c ldmsd_updtr.c ldmsd_strgp.c \
	ldmsd_smplr.c \
	ldmsd_plugin.c ldmsd_plugin.h ldmsd_group.c ldmsd_auth.c \
	ldmsd_daemon.c \
	ldmsd_env.c \
	ldmsd_listen.c
#	ldmsd_failover.c ldmsd_group.c
ldmsd_CFLAGS = $(AM_CFLAGS) -rdynamic
ldmsd_LDADD = $(CORE)/libldms.la librequest.la libldmsd_stream.la libsampler.la libstore.la libtranslator.la
ldmsd_LDFLAGS = $(AM_LDFLAGS) \
		$(top_builddir)/lib/src/zap/libzap.la \
		$(top_builddir)/lib/src/mmalloc/libmmalloc.la \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		$(top_builddir)/lib/src/json/libjson_util.la \
		$(top_builddir)/lib/src/ovis_event/libovis_event.la \
		$(top_builddir)/lib/src/ev/libev.la \
		-lpthread -lm

ldmsd_LDFLAGS += $(top_builddir)/lib/src/ovis_util/libovis_util.la \
		 $(top_builddir)/lib/src/ovis_ctrl/libovis_ctrl.la \
		 $(top_builddir)/lib/src/coll/libcoll.la \
		 -ldl

if ENABLE_RABBITV3
ldmsd_LDFLAGS += -lrabbitmq
ldmsd_CFLAGS += -DENABLE_RABBITMQ
endif

#ldmsctl_SOURCES = ldmsctl.c
#ldmsctl_LDADD = $(CORE)/libldms.la librequest.la
#ldmsctl_LDFLAGS = $(AM_LDFLAGS) @LDFLAGS_READLINE@ \
#		  $(top_builddir)/lib/src/ovis_util/libovis_util.la \
#		  $(top_builddir)/lib/src/ovis_ctrl/libovis_ctrl.la \
#		  $(top_builddir)/lib/src/coll/libcoll.la \
#		  $(top_builddir)/lib/src/mmalloc/libmmalloc.la \
#		  $(top_builddir)/lib/src/zap/libzap.la \
#		  $(top_builddir)/lib/src/json/libjson_util.la \
#		  -lm -pthread @LDFLAGS_GETTIME@
ldmsd_sos_init_SOURCES = ldmsd_sos_init.c
ldmsd_sos_init_CFLAGS = @SOS_INCDIR_FLAG@ $(AM_CFLAGS)
ldmsd_sos_init_LDADD = -lsos -lods -lpthread @SOS_LIBDIR_FLAG@ @SOS_LIB64DIR_FLAG@
ldmsd_sos_init_LDFLAGS = $(AM_LDFLAGS)

lib_LTLIBRARIES += libplugattr.la
libplugattr_la_SOURCES = ldmsd_plugattr.c ldmsd_plugattr.h
libplugattr_la_LIBADD = $(top_builddir)/lib/src/ovis_util/libovis_util.la \
			$(top_builddir)/lib/src/coll/libcoll.la

test_plugattr_SOURCES = ldmsd_plugattr.c ldmsd_plugattr.h
test_plugattr_CFLAGS = -DTEST_PLUGATTR $(AM_CFLAGS)
test_plugattr_LDADD = -lm -lpthread \
		      $(top_builddir)/lib/src/ovis_util/libovis_util.la \
		      $(top_builddir)/lib/src/coll/libcoll.la
test_plugattr_LDFLAGS = $(AM_LDFLAGS)

# sampler plugin
ldmsdinclude_HEADERS += ldmsd_sampler.h
libsampler_la_SOURCES = ldmsd_sampler.c
libsampler_la_LIBADD = $(CORE)/libldms.la
libsampler_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libsampler.la

# store plugin
ldmsdinclude_HEADERS += ldmsd_store.h
libstore_la_SOURCES = ldmsd_store.c
libstore_la_LIBADD = $(CORE)/libldms.la
libstore_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libstore.la

# translator plugin
ldmsdinclude_HEADERS += ldmsd_translator.h
libtranslator_la_SOURCES = ldmsd_translator.c
libtranslator_la_LIBADD = $(top_builddir)/lib/src/json/libjson_util.la
libtranslator_la_CFLAGS = $(AM_CFLAGS)
pkglib_LTLIBRARIES += libtranslator.la

EXTRA_DIST = ldmsd-sampler.md \
	     ldmsd-aggregator.md \
	     ldmsd-sampler-dev.md \
	     ldmsd-store-dev.md


# make sym links for aggd scripting support
install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsd ldms-aggd
#	cd $(DESTDIR)$(sbindir) && $(LN_S) -f ldmsctl ldmsaggctl

check-local:
	LD_LIBRARY_PATH=$(DESTDIR)$(libdir) ./test_plugattr $(srcdir)/input/test_plugattr.txt

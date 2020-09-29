pkglib_LTLIBRARIES =
dist_man7_MANS=

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la \
		../../core/libldms.la \
		@LDFLAGS_GETTIME@



if ENABLE_HELLO_STREAM

libhello_sampler_la_SOURCES = hello_sampler.c ../sampler_base.c
libhello_sampler_la_LIBADD = $(COMMON_LIBADD) \
			     $(top_builddir)/lib/src/ovis_json/libovis_json.la
pkglib_LTLIBRARIES += libhello_sampler.la
dist_man7_MANS += Plugin_hello_sampler.man

sbin_PROGRAMS = hello_publisher hello_cat_publisher
hello_publisher_SOURCES = hello_publisher.c
hello_publisher_LDADD = $(COMMON_LIBADD) \
			$(top_builddir)/lib/src/ovis_json/libovis_json.la \
			../../ldmsd/libldmsd_stream.la
hello_publisher_LDFLAGS = $(AM_LDFLAGS) -pthread
dist_man7_MANS += hello_publisher.man

hello_cat_publisher_SOURCES = hello_cat_publisher.c
hello_cat_publisher_LDADD = $(COMMON_LIBADD) \
			    $(top_builddir)/lib/src/ovis_json/libovis_json.la \
			    ../../ldmsd/libldmsd_stream.la
hello_cat_publisher_LDFLAGS = $(AM_LDFLAGS) -pthread
dist_man7_MANS += hello_cat_publisher.man

SUBDIRS = stream_configs

docversionexamplesdir = $(docdir)-$(PACKAGE_VERSION)/examples/hello_sampler_util
dist_docversionexamples_DATA = parser.pl

endif

EXTRA_DIST = \
	hello_publisher.man hello_cat_publisher.man Plugin_hello_sampler.man

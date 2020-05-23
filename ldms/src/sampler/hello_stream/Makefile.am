pkglib_LTLIBRARIES =
dist_man7_MANS=

SUBDIRS = stream_configs

CORE = ../../core
SAMPLER= ../../sampler
AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@ \
	-I../ -I../../ -I$(srcdir)/../../ldmsd
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@

COMMON_LIBADD = $(CORE)/libldms.la ../../ldmsd/libldmsd_stream.la \
		@LDFLAGS_GETTIME@ -lovis_util -lcoll -lpthread

libhello_sampler_la_SOURCES = hello_sampler.c hello_sampler.h ../sampler_base.c
libhello_sampler_la_LIBADD = $(COMMON_LIBADD) -ljson_util
libhello_sampler_la_CFLAGS = $(AM_CFLAGS) -DSYSCONFDIR='"$(sysconfdir)"'
pkglib_LTLIBRARIES += libhello_sampler.la
dist_man7_MANS += Plugin_hello_sampler.man

sbin_PROGRAMS = hello_publisher hello_cat_publisher
hello_publisher_SOURCES = hello_publisher.c
hello_publisher_LDADD = $(COMMON_LIBADD) -ljson_util \
			      ../../ldmsd/libldmsd_stream.la
hello_publisher_CFLAGS = $(AM_CFLAGS)
	-DSYSCONFDIR='"$(sysconfdir)"'
dist_man7_MANS += hello_publisher.man

hello_cat_publisher_SOURCES = hello_cat_publisher.c
hello_cat_publisher_LDADD = $(COMMON_LIBADD) -ljson_util \
			      ../../ldmsd/libldmsd_stream.la
hello_cat_publisher_CFLAGS = $(AM_CFLAGS)
	-DSYSCONFDIR='"$(sysconfdir)"'
dist_man7_MANS += hello_cat_publisher.man

EXTRA_DIST = \
	parser.pl

HELLO_EXTRA_FILES = \
	${srcdir}/parser.pl


HELLO_DIR = $(DESTDIR)$(sysconfdir)/ldms/hello_sampler_util
install-data-local: $(HELLO_EXTRA_FILES)
	$(MKDIR_P) $(HELLO_DIR)
	$(INSTALL_DATA) $(HELLO_EXTRA_FILES) $(HELLO_DIR)/
	chmod 755 $(HELLO_DIR)/*

uninstall-local:
	rm -rf $(HELLO_DIR)

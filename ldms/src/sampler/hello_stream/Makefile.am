pkglib_LTLIBRARIES =
dist_man7_MANS=


AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD =  -lldms -lovis_util -lcoll \
		@LDFLAGS_GETTIME@


if ENABLE_HELLO_STREAM

libhello_sampler_la_SOURCES = hello_sampler.c hello_sampler.h ../sampler_base.c
libhello_sampler_la_LIBADD = $(COMMON_LIBADD) -lovis_json
pkglib_LTLIBRARIES += libhello_sampler.la
dist_man7_MANS += Plugin_hello_sampler.man

sbin_PROGRAMS = hello_publisher hello_cat_publisher
hello_publisher_SOURCES = hello_publisher.c
hello_publisher_LDADD = $(COMMON_LIBADD) -lovis_json \
			      ../../ldmsd/libldmsd_stream.la
dist_man7_MANS += hello_publisher.man

hello_cat_publisher_SOURCES = hello_cat_publisher.c
hello_cat_publisher_LDADD = $(COMMON_LIBADD) -lovis_json \
			      ../../ldmsd/libldmsd_stream.la
dist_man7_MANS += hello_cat_publisher.man

SUBDIRS = stream_configs


HELLO_EXTRA_FILES = \
	${srcdir}/parser.pl

HELLO_DIR = $(DESTDIR)$(sysconfdir)/ldms/hello_sampler_util
install-data-local: $(HELLO_EXTRA_FILES)
	$(MKDIR_P) $(HELLO_DIR)
	$(INSTALL_DATA) $(HELLO_EXTRA_FILES) $(HELLO_DIR)/
	chmod 755 $(HELLO_DIR)/*

uninstall-local:
	rm -rf $(HELLO_DIR)

endif


EXTRA_DIST = \
	parser.pl hello_publisher.man hello_cat_publisher.man Plugin_hello_sampler.man

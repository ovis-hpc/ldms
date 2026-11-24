include $(top_srcdir)/ldms/rules.mk

EXTRA_DIST = ldmsd_decomposition.rst

dist_man7_MANS = ldmsd_decomposition.man

# Build the config parser library before the decomposition plugins
SUBDIRS = parsers static as_is flex

CLEANFILES = $(dist_man7_MANS)

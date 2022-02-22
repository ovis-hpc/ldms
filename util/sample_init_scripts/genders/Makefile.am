
docdir = $(datadir)/doc/@PACKAGE@/sample_init_scripts/genders

if ENABLE_TOSS_SYSTEMD
SUBDIRS = src systemd sysv man examples
if ENABLE_LIBGENDERS
SUBDIRS += src
endif
endif

dist_doc_DATA = \
README

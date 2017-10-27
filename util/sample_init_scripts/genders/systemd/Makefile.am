
docdir = $(datadir)/doc/@PACKAGE@-@VERSION@/sample_init_scripts/genders/systemd
EXTRA_DIST = ldmsd-pre-systemd.in ldmsd-wrapper.sh.in ldms-manpatch.sh
DISTCLEANFILES = ldmsd-pre-systemd ldmsd-wrapper.sh
bin_SCRIPTS=
if ENABLE_LIBGENDERS
SUBDIRS = etc services
bin_SCRIPTS+= ldmsd-pre-systemd ldmsd-wrapper.sh
endif
bin_SCRIPTS += ldms-manpatch.sh


docdir = $(datadir)/doc/@PACKAGE@-@VERSION@/sample_init_scripts/genders/sysv/etc/init.d

EXTRA_DIST = ldmsd.in ldmsd.template.in
DISTCLEANFILES = ldmsd ldmsd.template

if ENABLE_TOSS_SYSTEMD
dist_doc_DATA = \
README \
ldmsd \
ldmsd.template
endif


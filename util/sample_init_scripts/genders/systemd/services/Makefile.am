
docdir = $(datadir)/doc/@PACKAGE@-@VERSION@/sample_init_scripts/genders/systemd/services

doc_DATA=ldmsd.service ldmsd@.service README.ldmsd

EXTRA_DIST = ldmsd.service.in ldmsd@.service.in

do_subst = @OVIS_SUBST_RULE@

ldmsd.service: ldmsd.service.in
	$(do_subst) < $< > $@

ldmsd@.service: ldmsd@.service.in
	$(do_subst) < $< > $@

if ENABLE_TOSS_SYSTEMD
dist_doc_DATA = README.ldmsd
endif


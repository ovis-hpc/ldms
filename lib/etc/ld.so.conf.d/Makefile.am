EXTRA_DIST = ovis-ld-so.conf.in

do_subst = @LDMS_SUBST_RULE@

ovis-ld-so.conf: ovis-ld-so.conf.in
	$(do_subst) < $< > $@

docdir = $(datadir)/doc/@PACKAGE@/sample_init_scripts/opt/etc

sysconfldsodir = $(docdir)/ld.so.conf.d
sysconfldso_DATA = ovis-ld-so.conf

if SYSCONFDIR_NOT_ETC
sysconfldsoddir = $(sysconfdir)/ld.so.conf.d
sysconfldsod_DATA = ovis-ld-so.conf
endif

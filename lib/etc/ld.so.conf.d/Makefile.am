EXTRA_DIST = ovis-ld-so.conf.in

do_subst = @LDMS_SUBST_RULE@

ovis-ld-so.conf: ovis-ld-so.conf.in
	$(do_subst) < $< > $@

sysconfldsodir = $(sysconfdir)/ld.so.conf.d
sysconfldso_DATA = ovis-ld-so.conf

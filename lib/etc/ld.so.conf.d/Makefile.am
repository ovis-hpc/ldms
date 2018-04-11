EXTRA_DIST = ovis-ld-so.conf.in

do_subst = @LIB_SUBST_RULE@

ovis-ld-so.conf: ovis-ld-so.conf.in
	$(do_subst) < $< > $@

install-data-local: ovis-ld-so.conf
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/ld.so.conf.d/
	$(INSTALL_DATA) $(builddir)/ovis-ld-so.conf $(DESTDIR)/$(sysconfdir)/ld.so.conf.d/

uninstall-local:
	rm -f $(DESTDIR)/$(sysconfdir)/ld.so.conf.d/ovis-ld-so.conf

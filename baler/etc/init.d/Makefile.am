EXTRA_DIST = balerd

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/init.d
	$(INSTALL_SCRIPT) $(builddir)/balerd $(DESTDIR)$(sysconfdir)/init.d/

uninstall-local:
	rm -f $(DESTDIR)$(sysconfdir)/init.d/balerd

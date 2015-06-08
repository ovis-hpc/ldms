install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/ld.so.conf.d/
	$(INSTALL_DATA) $(builddir)/ovis-ld-so.conf $(DESTDIR)/$(sysconfdir)/ld.so.conf.d/

uninstall-local:
	rm -f $(DESTDIR)/$(sysconfdir)/ovis-ld-so.conf

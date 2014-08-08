install-data-local:
	$(MKDIR_P) $(DESTDIR)/etc/ld.so.conf.d/
	$(INSTALL_DATA) $(builddir)/ovis-ld-so.conf $(DESTDIR)/etc/ld.so.conf.d/

uninstall-local:
	rm -f $(DESTDIR)/etc/ld.so.conf.d/ovis-ld-so.conf

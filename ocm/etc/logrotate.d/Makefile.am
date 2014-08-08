install-data-local:
	$(MKDIR_P) $(DESTDIR)/etc/ovis/logrotate.d/
	$(INSTALL_DATA) $(srcdir)/ocmd $(DESTDIR)/etc/ovis/logrotate.d

uninstall-local:
	rm -f $(DESTDIR)/etc/ovis/logrotate.d/ocmd

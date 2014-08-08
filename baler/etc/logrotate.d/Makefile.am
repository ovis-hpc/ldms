install-data-local:
	$(MKDIR_P) $(DESTDIR)/etc/ovis/logrotate.d
	$(INSTALL_DATA) $(srcdir)/balerd $(DESTDIR)/etc/ovis/logrotate.d/

uninstall-local:
	rm -f $(DESTDIR)/etc/ovis/logrotate.d/balerd

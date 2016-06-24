EXTRA_DIST = balerd
install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/ovis/logrotate.d
	$(INSTALL_DATA) $(srcdir)/balerd $(DESTDIR)/$(sysconfdir)/ovis/logrotate.d/

uninstall-local:
	rm -f $(DESTDIR)/etc/ovis/logrotate.d/balerd

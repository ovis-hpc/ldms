EXTRA_DIST = balerd

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(sysconfdir)/logrotate.d
	$(INSTALL_DATA) $(srcdir)/balerd $(DESTDIR)$(sysconfdir)/logrotate.d/

uninstall-local:
	rm -f $(DESTDIR)$(sysconfdir)/logrotate.d/balerd

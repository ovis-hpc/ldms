EXTRA_DIST = ovis-functions.sh set-ovis-variables.sh ovis-logrotate
install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/ovis
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/profile.d
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/cron.hourly
	$(INSTALL_SCRIPT) $(builddir)/set-ovis-variables.sh $(DESTDIR)/$(sysconfdir)/profile.d
	$(INSTALL_DATA) $(srcdir)/ovis-functions.sh $(DESTDIR)/$(sysconfdir)/ovis
	$(INSTALL_SCRIPT) $(builddir)/ovis-logrotate $(DESTDIR)/$(sysconfdir)/cron.hourly

uninstall-local:
	rm -f $(DESTDIR)/$(sysconfdir)/profile.d/set-ovis-variables.sh
	rm -f $(DESTDIR)/$(sysconfdir)/ovis/ovis-functions.sh
	rm -f $(DESTDIR)/$(sysconfdir)/cron.hourly/ovis-logrotate

install-data-local:
	$(MKDIR_P) $(DESTDIR)/etc/ovis
	$(MKDIR_P) $(DESTDIR)/etc/profile.d
	$(MKDIR_P) $(DESTDIR)/etc/cron.hourly
	$(INSTALL_SCRIPT) $(builddir)/set-ovis-variables.sh $(DESTDIR)/etc/profile.d
	$(INSTALL_DATA) $(srcdir)/ovis-functions.sh $(DESTDIR)/etc/ovis
	$(INSTALL_SCRIPT) $(builddir)/ovis-logrotate $(DESTDIR)/etc/cron.hourly

uninstall-local:
	rm -f $(DESTDIR)/etc/profile.d/set-ovis-variables.sh
	rm -f $(DESTDIR)/etc/ovis/ovis-functions.sh
	rm -f $(DESTDIR)/etc/cron.hourly/ovis-logrotate

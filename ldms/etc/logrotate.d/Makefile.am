install-data-local:
	$(MKDIR_P) $(DESTDIR)/etc/ovis/logrotate.d/
	$(INSTALL_DATA) $(srcdir)/ldmsd_aggregator $(DESTDIR)/etc/ovis/logrotate.d/
	$(INSTALL_DATA) $(srcdir)/ldmsd_sampler $(DESTDIR)/etc/ovis/logrotate.d/

uninstall-local:
	rm -f $(DESTDIR)/etc/ovis/logrotate.d/ldmsd_aggregator
	rm -f $(DESTDIR)/etc/ovis/logrotate.d/ldmsd_sampler

EXTRA_DIST = ldmsd_aggregator ldmsd_sampler

install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/ovis/logrotate.d/
	$(INSTALL_DATA) $(srcdir)/ldmsd_aggregator $(DESTDIR)/$(sysconfdir)/ovis/logrotate.d/
	$(INSTALL_DATA) $(srcdir)/ldmsd_sampler $(DESTDIR)/$(sysconfdir)/ovis/logrotate.d/

uninstall-local:
	rm -f $(DESTDIR)/$(sysconfdir)/ovis/logrotate.d/ldmsd_aggregator
	rm -f $(DESTDIR)/$(sysconfdir)/ovis/logrotate.d/ldmsd_sampler

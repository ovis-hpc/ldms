EXTRA_DIST = ldmsd_aggregator ldmsd_sampler
docdir = $(datadir)/doc/@PACKAGE@/sample_init_scripts/opt/etc

install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(docdir)/ovis/logrotate.d/
	$(INSTALL_DATA) $(srcdir)/ldmsd_aggregator $(DESTDIR)/$(docdir)/ovis/logrotate.d/
	$(INSTALL_DATA) $(srcdir)/ldmsd_sampler $(DESTDIR)/$(docdir)/ovis/logrotate.d/

uninstall-local:
	rm -f $(DESTDIR)/$(docdir)/ovis/logrotate.d/ldmsd_aggregator
	rm -f $(DESTDIR)/$(docdir)/ovis/logrotate.d/ldmsd_sampler

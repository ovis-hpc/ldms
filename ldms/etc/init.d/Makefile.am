install-data-local:
	$(MKDIR_P) $(DESTDIR)/etc/init.d/
	$(INSTALL_SCRIPT) $(builddir)/ldmsd_aggregator $(DESTDIR)/etc/init.d/
	$(INSTALL_SCRIPT) $(builddir)/ldmsd_sampler $(DESTDIR)/etc/init.d/

uninstall-local:
	rm -f $(DESTDIR)/etc/init.d/ldmsd_aggregator
	rm -f $(DESTDIR)/etc/init.d/ldmsd_sampler

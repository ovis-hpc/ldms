install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/init.d/
	$(INSTALL_SCRIPT) $(builddir)/ldmsd_aggregator $(DESTDIR)/$(sysconfdir)/init.d/
	$(INSTALL_SCRIPT) $(builddir)/ldmsd_sampler $(DESTDIR)/$(sysconfdir)/init.d/

uninstall-local:
	rm -f $(DESTDIR)/$(sysconfdir)/init.d/ldmsd_aggregator
	rm -f $(DESTDIR)/$(sysconfdir)/init.d/ldmsd_sampler

docdir = $(datadir)/doc/@PACKAGE@/sample_init_scripts/opt/etc

install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(docdir)/init.d/
	$(INSTALL_SCRIPT) $(builddir)/ldmsd_aggregator $(DESTDIR)/$(docdir)/init.d/
	$(INSTALL_SCRIPT) $(builddir)/ldmsd_sampler $(DESTDIR)/$(docdir)/init.d/

uninstall-local:
	rm -f $(DESTDIR)/$(docdir)/init.d/ldmsd_aggregator
	rm -f $(DESTDIR)/$(docdir)/init.d/ldmsd_sampler

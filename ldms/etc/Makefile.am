
install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(prefix)/etc/init.d/
	$(INSTALL_SCRIPT) $(builddir)/init.d/ldmsd_aggregator $(DESTDIR)/$(prefix)/etc/init.d/
	$(INSTALL_SCRIPT) $(builddir)/init.d/ldmsd_sampler $(DESTDIR)/$(prefix)/etc/init.d/
	$(LN_S) -f $(DESTDIR)/$(prefix)/etc/init.d/ldmsd_aggregator /etc/init.d/ldmsd_aggregator
	$(LN_S) -f $(DESTDIR)/$(prefix)/etc/init.d/ldmsd_sampler /etc/init.d/ldmsd_sampler


install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(prefix)/etc/init.d
	$(INSTALL_SCRIPT) $(builddir)/init.d/komondor $(DESTDIR)/$(prefix)/etc/init.d/
	$(LN_S) -f $(DESTDIR)/$(prefix)/etc/init.d/komondor /etc/init.d/

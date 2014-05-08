install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(prefix)/etc/init.d
	$(INSTALL_SCRIPT) $(builddir)/init.d/balerd $(DESTDIR)/$(prefix)/etc/init.d/
	$(LN_S) -f $(DESTDIR)/$(prefix)/etc/init.d/balerd /etc/init.d/

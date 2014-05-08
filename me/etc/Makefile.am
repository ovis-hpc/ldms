install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(prefix)/etc/init.d
	$(INSTALL_SCRIPT) $(builddir)/init.d/me $(DESTDIR)/$(prefix)/etc/init.d/
	$(LN_S) -f $(DESTDIR)/$(prefix)/etc/init.d/me /etc/init.d/

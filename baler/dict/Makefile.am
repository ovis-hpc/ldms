EXTRA_DIST = eng-dictionary

install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(prefix)/etc/ovis
	$(INSTALL_DATA) $(srcdir)/eng-dictionary $(DESTDIR)/$(prefix)/etc/ovis

uninstall-local:
	rm -f $(DESTDIR)/$(prefix)/etc/ovis/eng-dictionary

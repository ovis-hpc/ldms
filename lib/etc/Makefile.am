
install-data-local:
	$(MKDIR_P) $(DESTDIR)/etc/profile.d/
	$(MKDIR_P) $(DESTDIR)/etc/ovis
	$(MKDIR_P) $(DESTDIR)/etc/ld.so.conf.d/
	$(INSTALL_SCRIPT) $(builddir)/ovis/set-ovis-variables.sh $(DESTDIR)/etc/profile.d/
	$(INSTALL_DATA) $(srcdir)/ovis/ovis-functions.sh $(DESTDIR)/etc/ovis
	$(INSTALL_DATA) $(builddir)/ld.so.conf.d/ovis-ld-so.conf $(DESTDIR)/etc/ld.so.conf.d/

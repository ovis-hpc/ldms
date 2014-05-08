
install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(prefix)/etc/profile.d/
	$(MKDIR_P) $(DESTDIR)/$(prefix)/etc/ovis
	$(MKDIR_P) $(DESTDIR)/$(prefix)/etc/ld.so.conf.d/
	$(INSTALL_SCRIPT) $(builddir)/ovis/set-ovis-variables.sh $(DESTDIR)/$(prefix)/etc/profile.d/
	$(INSTALL_DATA) $(srcdir)/ovis/ovis-functions.sh $(DESTDIR)/$(prefix)//etc/ovis
	$(INSTALL_DATA) $(builddir)/ld.so.conf.d/ovis-ld-so.conf $(DESTDIR)/$(prefix)//etc/ld.so.conf.d/
	$(LN_S) -f $(DESTDIR)/$(prefix)/etc/profile.d/set-ovis-variables.sh /etc/profile.d/
	$(LN_S) -f $(DESTDIR)/$(prefix)/etc/ovis/ovis-functions.sh /etc/ovis/
	$(LN_S) -f $(DESTDIR)/$(prefix)/etc/ld.so.conf.d/ovis-ld-so.conf /etc/ld.so.conf.d/

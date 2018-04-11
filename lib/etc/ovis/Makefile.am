EXTRA_DIST = ovis-functions.sh \
	     set-ovis-variables.sh.in

do_subst = @LIB_SUBST_RULE@

set-ovis-variables.sh: set-ovis-variables.sh.in
	$(do_subst) < $< > $@

install-data-local: set-ovis-variables.sh
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/ovis
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/profile.d
	$(INSTALL_SCRIPT) set-ovis-variables.sh $(DESTDIR)/$(sysconfdir)/profile.d
	$(INSTALL_DATA) $(srcdir)/ovis-functions.sh $(DESTDIR)/$(sysconfdir)/ovis

uninstall-local:
	rm -f $(DESTDIR)/$(sysconfdir)/profile.d/set-ovis-variables.sh
	rm -f $(DESTDIR)/$(sysconfdir)/ovis/ovis-functions.sh

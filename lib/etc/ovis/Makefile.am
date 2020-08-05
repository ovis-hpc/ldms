EXTRA_DIST = ovis-functions.sh \
	     set-ovis-variables.sh.in

all: set-ovis-variables.sh

do_subst = @OVIS_SUBST_RULE@

set-ovis-variables.sh: set-ovis-variables.sh.in
	$(do_subst) < $< > $@

install-data-local:
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/ovis
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/profile.d
	$(INSTALL_SCRIPT) set-ovis-variables.sh $(DESTDIR)/$(sysconfdir)/profile.d
	$(INSTALL_DATA) $(srcdir)/ovis-functions.sh $(DESTDIR)/$(sysconfdir)/ovis

uninstall-local:
	rm -f $(DESTDIR)/$(sysconfdir)/profile.d/set-ovis-variables.sh
	rm -f $(DESTDIR)/$(sysconfdir)/ovis/ovis-functions.sh

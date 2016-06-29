EXTRA_DIST = ovis-functions.sh \
	     set-ovis-variables.sh.in

do_subst = sed -e 's,[@]datadir[@],$(datadir),g' \
	   -e 's,[@]localstatedir[@],$(localstatedir),g' \
	   -e 's,[@]sysconfdir[@],$(sysconfdir),g' \
	   -e 's,[@]pythondir[@],$(pythondir),g' \
	   -e 's,[@]libdir[@],$(libdir),g' \
	   -e 's,[@]bindir[@],$(bindir),g' \
	   -e 's,[@]sbindir[@],$(sbindir),g'

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

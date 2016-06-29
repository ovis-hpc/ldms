EXTRA_DIST = ovis-ld-so.conf.in

do_subst = sed -e 's,[@]datadir[@],$(datadir),g' \
	   -e 's,[@]localstatedir[@],$(localstatedir),g' \
	   -e 's,[@]libdir[@],$(libdir),g' \
	   -e 's,[@]sysconfdir[@],$(sysconfdir),g' \
	   -e 's,[@]pythondir[@],$(pythondir),g' \
	   -e 's,[@]bindir[@],$(bindir),g' \
	   -e 's,[@]sbindir[@],$(sbindir),g'

ovis-ld-so.conf: ovis-ld-so.conf.in
	$(do_subst) < $< > $@

install-data-local: ovis-ld-so.conf
	$(MKDIR_P) $(DESTDIR)/$(sysconfdir)/ld.so.conf.d/
	$(INSTALL_DATA) $(builddir)/ovis-ld-so.conf $(DESTDIR)/$(sysconfdir)/ld.so.conf.d/

uninstall-local:
	rm -f $(DESTDIR)/$(sysconfdir)/ld.so.conf.d/ovis-ld-so.conf

ACLOCAL_AMFLAGS = -I m4

SYSTEMD_DIR = $(DESTDIR)$(sysconfdir)/systemd/system

do_subst = sed -e 's,[@]datadir[@],$(datadir),g' \
	   -e 's,[@]localstatedir[@],$(localstatedir),g' \
	   -e 's,[@]sysconfdir[@],$(sysconfdir),g' \
	   -e 's,[@]ZAP_LIBPATH[@],$(ZAP_LIBPATH),g' \
	   -e 's,[@]sbindir[@],$(sbindir),g'

EXTRA_DIST = balerd.master.service.in \
	     balerd.slave.service.in \
	     bhttpd.service.in

SERVICE_FILES = balerd.master.service \
		balerd.slave.service \
		bhttpd.service

# rules to generate service file
%.service: $(srcdir)/%.service.in
	$(do_subst) < $< > $@

install-data-local: $(SERVICE_FILES)
	$(MKDIR_P) $(SYSTEMD_DIR)
	$(INSTALL_DATA) $(SERVICE_FILES) $(SYSTEMD_DIR)

uninstall-local:
	cd $(SYSTEMD_DIR) && rm -f $(SERVICE_FILES)

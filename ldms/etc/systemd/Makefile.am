SYSTEMD_DIR = $(DESTDIR)$(sysconfdir)/systemd/system

do_subst = sed -e 's,[@]datadir[@],$(datadir),g' \
	   -e 's,[@]localstatedir[@],$(localstatedir),g' \
	   -e 's,[@]sysconfdir[@],$(sysconfdir),g' \
	   -e 's,[@]LDMSD_PLUGIN_LIBPATH[@],$(LDMSD_PLUGIN_LIBPATH),g' \
	   -e 's,[@]ZAP_LIBPATH[@],$(ZAP_LIBPATH),g' \
	   -e 's,[@]sbindir[@],$(sbindir),g'

SVC_FILES = $(builddir)/ldmsd.sampler.service \
	    $(builddir)/ldmsd.aggregator.service

$(builddir)/%.service: $(srcdir)/%.service.in
	$(do_subst) < $< > $@

EXTRA_DIST = ldmsd.aggregator.service.in \
	     ldmsd.sampler.service.in

install-data-local: $(SVC_FILES)
	$(MKDIR_P) $(SYSTEMD_DIR)
	$(INSTALL_DATA) $(SVC_FILES) $(SYSTEMD_DIR)/

uninstall-local:
	rm -f $(SYSTEMD_DIR)/ldmsd.sampler.service
	rm -f $(SYSTEMD_DIR)/ldmsd.aggregator.service

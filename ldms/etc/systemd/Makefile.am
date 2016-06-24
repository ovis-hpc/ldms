SYSTEMD_DIR = $(DESTDIR)$(sysconfdir)/systemd/system
CONF_DIR = $(DESTDIR)$(sysconfdir)

do_subst = sed -e 's,[@]datadir[@],$(datadir),g' \
	   -e 's,[@]localstatedir[@],$(localstatedir),g' \
	   -e 's,[@]sysconfdir[@],$(sysconfdir),g' \
	   -e 's,[@]LDMSD_PLUGIN_LIBPATH[@],$(LDMSD_PLUGIN_LIBPATH),g' \
	   -e 's,[@]ZAP_LIBPATH[@],$(ZAP_LIBPATH),g' \
	   -e 's,[@]sbindir[@],$(sbindir),g'

SVC_ENV_FILES = $(builddir)/ldmsd.sampler.service \
		$(builddir)/ldmsd.sampler.env \
		$(builddir)/ldmsd.aggregator.service \
		$(builddir)/ldmsd.aggregator.env

$(builddir)/%.service: $(srcdir)/%.service.in
	$(do_subst) < $< > $@

$(builddir)/%.env: $(srcdir)/%.env.in
	$(do_subst) < $< > $@

EXTRA_DIST = ldmsauth.conf \
	     ldms.cfg \
	     ldmsplugin.sampler.conf \
	     ldmsplugin.aggregator.conf \
	     ldmsd.aggregator.env.in \
	     ldmsd.aggregator.service.in \
	     ldmsd.sampler.env.in \
	     ldmsd.sampler.service.in

install-data-local: $(SVC_ENV_FILES)
	$(MKDIR_P) $(SYSTEMD_DIR)
	$(INSTALL_DATA) $(SVC_ENV_FILES) $(SYSTEMD_DIR)/
	$(INSTALL) -m 600 $(srcdir)/ldmsauth.conf $(CONF_DIR)
	$(INSTALL) -m 644 $(srcdir)/ldms.cfg $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/ldmsplugin.sampler.conf $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/ldmsplugin.aggregator.conf $(CONF_DIR)

uninstall-local:
	rm -f $(SYSTEMD_DIR)/ldmsd.sampler.service
	rm -f $(SYSTEMD_DIR)/ldmsd.sampler.env
	rm -f $(SYSTEMD_DIR)/ldmsd.aggregator.service
	rm -f $(SYSTEMD_DIR)/ldmsd.aggregator.env
	rm -f $(CONF_DIR)/ldmsauth.conf
	rm -f $(CONF_DIR)/ldms.cfg
	rm -f $(CONF_DIR)/ldmsplugin.sampler.conf
	rm -f $(CONF_DIR)/ldmsplugin.aggregator.conf

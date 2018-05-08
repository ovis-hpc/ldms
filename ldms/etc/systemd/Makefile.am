SYSTEMD_DIR = $(DESTDIR)$(sysconfdir)/systemd/system

do_subst = @LDMS_SUBST_RULE@

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

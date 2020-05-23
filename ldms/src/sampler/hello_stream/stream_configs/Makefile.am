
if ENABLE_HELLO_STREAM


STREAM_FILES = \
	$(srcdir)/hello_stream1.conf \
	$(srcdir)/hello_stream_store.conf \
	${srcdir}/README


HELLO_CONF_DIR = $(DESTDIR)$(sysconfdir)/ldms/hello_sampler_util/stream_configs
install-data-local: $(STREAM_FILES)
	$(MKDIR_P) $(HELLO_CONF_DIR)
	$(INSTALL_DATA) $(STREAM_FILES) $(HELLO_CONF_DIR)/


uninstall-local:
	rm -rf $(STREAM_DIR)


endif

EXTRA_DIST = \
	hello_stream1.conf \
	hello_stream_store.conf \
	README

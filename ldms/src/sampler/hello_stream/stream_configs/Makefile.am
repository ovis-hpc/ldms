
if ENABLE_HELLO_STREAM


STREAM_FILES = \
	$(srcdir)/hello_stream1.conf \
	$(srcdir)/hello_stream_store.conf \
	${srcdir}/README

OPV=-$(PACKAGE_VERSION)
HELLO_CONF_DIR = $(docdir)$(OPV)/examples/hello_sampler_util/stream_configs
install-data-local: $(STREAM_FILES)
	$(MKDIR_P) $(DESTDIR)$(HELLO_CONF_DIR)
	$(INSTALL_DATA) $(STREAM_FILES) $(DESTDIR)$(HELLO_CONF_DIR)/


uninstall-local:
	for i in $(STREAM_FILES); do \
		rm -rf $(DESTDIR)$(HELLO_CONF_DIR)/$$i; \
	done


endif

EXTRA_DIST = \
	hello_stream1.conf \
	hello_stream_store.conf \
	README

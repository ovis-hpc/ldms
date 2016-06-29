EXTRA_DIST = eng-dictionary

DICT_DIR = $(DESTDIR)$(sysconfdir)/baler

install-data-local:
	$(MKDIR_P) $(DICT_DIR)
	$(INSTALL_DATA) $(srcdir)/eng-dictionary $(DICT_DIR)

uninstall-local:
	rm -f $(DICT_DIR)/eng-dictionary

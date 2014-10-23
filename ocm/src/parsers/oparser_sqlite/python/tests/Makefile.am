bin_SCRIPTS = test_oconvert_sqlite.py test_osqlite_util.py

OCSQLITE_TEST_DIR = $(DESTDIR)/${prefix}/var/ocsqlite_tests
OCSQLITE_TEST_TAR = oconf_sqlite_conf_test.tgz

install-data-hook:
	$(MKDIR_P) $(OCSQLITE_TEST_DIR);
	$(INSTALL_DATA) $(srcdir)/$(OCSQLITE_TEST_TAR) $(OCSQLITE_TEST_DIR);
	cd $(OCSQLITE_TEST_DIR); \
	$(TAR) -tzf $(OCSQLITE_TEST_TAR) > uninstall_manifest.txt; \
	$(TAR) --no-same-owner -xzf $(OCSQLITE_TEST_TAR); \
	rm -f $(OCSQLITE_TEST_TAR)

install-exec-hook:
	$(MKDIR_P) $(OCSQLITE_TEST_DIR)


uninstall-local:
	cd $(OCSQLITE_TEST_DIR); \
	cat uninstall_manifest.txt | sed --expression='s/ /\\ /g' | xargs rm -fr; \
	rm -f uninstall_manifest.txt; \
	rm -fr $(OCSQLITE_TEST_DIR)

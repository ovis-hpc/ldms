
# consider adding ldmsd to bin_SCRIPTS if ldmsd script ever stabilizes
EXTRA_DIST=@OVIS_EXTRA_DIST@ examples lsdate

if ENABLE_SCRIPTS
sbin_SCRIPTS= ldms-wrapper
bin_SCRIPTS= envldms.sh
bin_SCRIPTS += lsdate
bin_SCRIPTS += ldms-static-test.sh

DISTCLEANFILES = $(bin_SCRIPTS) $(sbin_SCRIPTS)
OPV=-$(PACKAGE_VERSION)

bin_SCRIPTS += ldms_ban.sh
bin_SCRIPTS += ldms_plugins_list.sh
bin_SCRIPTS += ldms-py-subset_test.sh
bin_SCRIPTS += ldms-py-syslog.sh
bin_SCRIPTS += ldms-meminfo.sh
bin_SCRIPTS += ldms-py-rename.sh

testsets = install-serial
untestsets = uninstall-serial
if ENABLE_SLURMTEST
testsets += install-slurm
untestsets += uninstall-slurm
bin_SCRIPTS += pll-ldms-static-test.sh
endif

lsdate: ${srcdir}/lsdate
	cp ${srcdir}/lsdate .

install-data-local: $(testsets)

install-serial:
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/examples/static-test && \
	uf=$(DESTDIR)$(docdir)$(OPV)/examples/static-test/.uninst.ldms && \
	touch $$uf && \
	for f in $(srcdir)/examples/* ; do \
		if test "$$f" = "$(srcdir)/examples/README"; then \
			continue; \
		fi; \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/examples/static-test; \
		echo $$(basename $$f) >> $$uf; \
	done
	$(INSTALL_DATA) $(srcdir)/examples/.canned $(DESTDIR)$(docdir)$(OPV)/examples/static-test

install-slurm:
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/examples/slurm-test && \
	uf=$(DESTDIR)$(docdir)$(OPV)/examples/slurm-test/.uninst.ldms && \
	touch $$uf && \
	for f in $(srcdir)/slurm-examples/* ; do \
		if test "$$f" = "$(srcdir)/slurm-examples/README"; then \
			continue; \
		fi; \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/examples/slurm-test; \
		echo $$(basename $$f) >> $$uf; \
	done
	$(INSTALL_DATA) $(srcdir)/slurm-examples/.canned $(DESTDIR)$(docdir)$(OPV)/examples/slurm-test

install-exec-hook:
	mv $(DESTDIR)/$(sbindir)/ldms-wrapper $(DESTDIR)/$(sbindir)/.ldms-wrapper

uninstall-serial:
	echo removing static-test files:
	while read f; do \
		echo $$f; \
		$(RM) $(DESTDIR)$(docdir)$(OPV)/examples/static-test/$$f; \
	done < $(DESTDIR)$(docdir)$(OPV)/examples/static-test/.uninst.ldms
	$(RM) $(DESTDIR)$(docdir)$(OPV)/examples/static-test/.canned
	$(RM) $(DESTDIR)$(docdir)$(OPV)/examples/static-test/.uninst.ldms

uninstall-slurm:
	echo removing slurm-test files:
	while read f; do \
		echo $$f; \
		$(RM) $(DESTDIR)$(docdir)$(OPV)/examples/slurm-test/$$f; \
	done < $(DESTDIR)$(docdir)$(OPV)/examples/slurm-test/.uninst.ldms
	$(RM) $(DESTDIR)$(docdir)$(OPV)/examples/slurm-test/.canned
	$(RM) $(DESTDIR)$(docdir)$(OPV)/examples/slurm-test/.uninst.ldms

uninstall-local: $(untestsets)
	echo removing other files:
	$(RM) $(DESTDIR)/$(sbindir)/.ldms-wrapper
endif

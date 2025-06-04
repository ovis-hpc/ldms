include $(top_srcdir)/ldms/rules.mk

dist_man1_MANS = ldms-csv-anonymize.man ldms-csv-export-sos.man
dist_man8_MANS =
# consider adding ldmsd to bin_SCRIPTS if ldmsd script ever stabilizes
EXTRA_DIST=examples lsdate slurm-examples
EXTRA_DIST += ldms-csv-anonymize ldms-csv-export-sos
EXTRA_DIST += ovis-roll-over.py ldmsd-check-env
EXTRA_DIST += ldms-reverse-conf.sh

if ENABLE_SCRIPTS
sbin_SCRIPTS= ldms-wrapper
bin_SCRIPTS = lsdate
bin_SCRIPTS += ldms-static-test.sh
bin_SCRIPTS += ldms-reverse-conf.sh
bin_SCRIPTS += ovis-roll-over.py
bin_SCRIPTS += ldms-csv-anonymize
bin_SCRIPTS += ldms-csv-export-sos
bin_SCRIPTS += ldmsd-check-env

DISTCLEANFILES = $(gen_scripts) $(sgen_scripts)

sgen_scripts = ldms-wrapper

gen_scripts = envldms.sh
gen_scripts += ldms_ban.sh
gen_scripts += ldms-plugins.sh
gen_scripts += ldms-py-subset_test.sh
gen_scripts += ldms-py-syslog.sh
gen_scripts += ldms-meminfo.sh
gen_scripts += ldms-py-varset.sh
gen_scripts += ldms-py-rename.sh
gen_scripts += ldms_local_opa2test.sh
gen_scripts += ldms-l2_test.sh

bin_SCRIPTS += $(gen_scripts)

testsets = install-serial
untestsets = uninstall-serial
if ENABLE_SLURMTEST
testsets += install-slurm
untestsets += uninstall-slurm
bin_SCRIPTS += pll-ldms-static-test.sh
dist_man8_MANS += ldms_pll-ldms-static-test.man
endif

dist_man8_MANS += ldms-static-test.man

CLEANFILES = $(dist_man1_MANS) $(dist_man1_MANS)

install-data-local: $(testsets)

install-serial:
	$(MKDIR_P) $(DESTDIR)$(docdir)/examples/static-test && \
	uf=$(DESTDIR)$(docdir)/examples/static-test/.uninst.ldms && \
	touch $$uf && \
	for f in $(srcdir)/examples/* ; do \
		if test "$$f" = "$(srcdir)/examples/README"; then \
			continue; \
		fi; \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)/examples/static-test; \
		echo $$(basename $$f) >> $$uf; \
	done
	$(INSTALL_DATA) $(srcdir)/examples/.canned $(DESTDIR)$(docdir)/examples/static-test

install-slurm:
	$(MKDIR_P) $(DESTDIR)$(docdir)/examples/slurm-test && \
	uf=$(DESTDIR)$(docdir)/examples/slurm-test/.uninst.ldms && \
	touch $$uf && \
	for f in $(srcdir)/slurm-examples/* ; do \
		if test "$$f" = "$(srcdir)/slurm-examples/README"; then \
			continue; \
		fi; \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)/examples/slurm-test; \
		echo $$(basename $$f) >> $$uf; \
	done
	$(INSTALL_DATA) $(srcdir)/slurm-examples/.canned $(DESTDIR)$(docdir)/examples/slurm-test

install-exec-hook:
	mv $(DESTDIR)/$(sbindir)/ldms-wrapper $(DESTDIR)/$(sbindir)/.ldms-wrapper

uninstall-serial:
	echo removing static-test files:
	while read f; do \
		echo $$f; \
		$(RM) $(DESTDIR)$(docdir)/examples/static-test/$$f; \
	done < $(DESTDIR)$(docdir)/examples/static-test/.uninst.ldms
	$(RM) $(DESTDIR)$(docdir)/examples/static-test/.canned
	$(RM) $(DESTDIR)$(docdir)/examples/static-test/.uninst.ldms

uninstall-slurm:
	echo removing slurm-test files:
	while read f; do \
		echo $$f; \
		$(RM) $(DESTDIR)$(docdir)/examples/slurm-test/$$f; \
	done < $(DESTDIR)$(docdir)/examples/slurm-test/.uninst.ldms
	$(RM) $(DESTDIR)$(docdir)/examples/slurm-test/.canned
	$(RM) $(DESTDIR)$(docdir)/examples/slurm-test/.uninst.ldms

uninstall-local: $(untestsets)
	echo removing other files:
	$(RM) $(DESTDIR)/$(sbindir)/.ldms-wrapper
endif

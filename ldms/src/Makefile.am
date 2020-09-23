ACLOCAL_AMFLAGS = -I m4
SUBDIRS =
EXTRA_DIST=ovis-ldms-configvars.sh.in
CLEANFILES = ovis-ldms-configvars.sh
VERSION=@VERSION@

ac_configure_args=@ac_configure_args@

do_subst = @LDMS_SUBST_RULE@

%.sh: %.sh.in
	$(do_subst) < $< > $@
	chmod 755 $@

SUBDIRS += third-plugins
SUBDIRS += core
SUBDIRS += ldmsd

if ENABLE_OVIS_AUTH
SUBDIRS += auth
endif

if ENABLE_STORE
SUBDIRS += store
endif

if ENABLE_SAMPLER
SUBDIRS += sampler
endif

if ENABLE_LDMS_TEST
SUBDIRS += test
EXTRA_DIST+= ldms-static-test-list.sh.in

FILES= ldms-static-test-list.sh
FILES+= ldms-static-test-bypass
endif

SUBDIRS += contrib

distclean-local:
	-rm ovis-ldms-configure-args
	-rm ovis-ldms-configure-env

ovis-ldms-configure-args: Makefile
	for i in @ac_configure_args@; do \
		(echo -n $$i |sed -E 's/(=)(.*)$$/="\2"/' | sed -e s/^\'// -e s/\'$$// ) | grep -v '^--' >> ovis-ldms-configure-env; \
		(echo -n \'$$i |sed -E 's/(=)(.*)$$/="\2"/' && echo \') | grep -v srcdir | grep '^.--' >> ovis-ldms-configure-args; \
	done

install-data-hook: ovis-ldms-configvars.sh ovis-ldms-configure-args $(FILES)
	$(MKDIR_P) $(DESTDIR)$(libdir)
	$(MKDIR_P) $(DESTDIR)$(pkglibdir)
	$(INSTALL) -m 755 ovis-ldms-configvars.sh $(DESTDIR)$(libdir)
	$(INSTALL) -m 644 ovis-ldms-configure-args $(DESTDIR)$(pkglibdir)
	$(INSTALL) -m 644 ovis-ldms-configure-env $(DESTDIR)$(pkglibdir)
	$(INSTALL) -m 755 ldms-static-test-list.sh $(DESTDIR)$(pkglibdir)
	$(INSTALL) -m 644 ldms-static-test-bypass $(DESTDIR)$(pkglibdir)

uninstall-hook:
	rm $(DESTDIR)$(libdir)/ovis-ldms-configvars.sh
	rm $(DESTDIR)$(pkglibdir)/ovis-ldms-configure-env
	rm $(DESTDIR)$(pkglibdir)/ovis-ldms-configure-args
	rm $(DESTDIR)$(pkglibdir)/$(FILES)

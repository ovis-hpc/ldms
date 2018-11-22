nobase_include_HEADERS = ovis-ldms-config.h
ACLOCAL_AMFLAGS = -I m4
SUBDIRS =
EXTRA_DIST=ovis-ldms-configvars.sh.in
CLEANFILES = ovis-ldms-configvars.sh
DISTCLEANFILES = $(nobase_include_HEADERS)

do_subst = @LDMS_SUBST_RULE@

%.sh: %.sh.in
	$(do_subst) < $< > $@
	chmod 755 $@

if ENABLE_CORE
SUBDIRS += core
SUBDIRS += auth
endif

if ENABLE_LDMSD
SUBDIRS += ldmsd
endif

if ENABLE_STORE
SUBDIRS += store
endif

if ENABLE_SAMPLER
SUBDIRS += sampler
endif

if ENABLE_LDMS_TEST
SUBDIRS += test
endif

install-data-hook: ovis-ldms-configvars.sh
	$(MKDIR_P) $(DESTDIR)$(libdir)
	$(INSTALL) -m 755 ovis-ldms-configvars.sh $(DESTDIR)$(libdir)

uninstall-hook:
	rm $(DESTDIR)$(libdir)/ovis-ldms-configvars.sh

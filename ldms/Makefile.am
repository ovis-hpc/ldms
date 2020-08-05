EXTRA_DIST = README.md

ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

if ENABLE_SCRIPTS
SUBDIRS += scripts
endif
if ENABLE_ETC
SUBDIRS += etc
endif

if ENABLE_LDMS_PYTHON
SUBDIRS += python
endif

LDMS = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
LDMS_TARBALL = $(LDMS).tar.gz
OPV=-$(PACKAGE_VERSION)

if ENABLE_DEVELOPER

install-exec-hook:
	@echo "*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#"
	@echo "INSTALL COMPLETED."
	@echo "Errors seen after this matter to plugin or core developers only."
	@echo "*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#"
	-$(bindir)/ldms_ban.sh $(srcdir)/../

endif

banned:
	bash $(srcdir)/scripts/ldms_ban.sh.in $(srcdir)/../

if ENABLE_DOC
SUBDIRS += doc
endif

if ENABLE_DOC_MAN
SUBDIRS += man
endif


install-data-local:
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)
	for f in AUTHORS README.md COPYING ChangeLog ; do \
		$(INSTALL_DATA) ${srcdir}/$$f $(DESTDIR)$(docdir)$(OPV); \
	done

uninstall-hook:
	rm $(PEDIGREE)
	for f in AUTHORS README.md COPYING ChangeLog ; do \
		rm $(DESTDIR)$(docdir)$(OPV)/$$f; \
	done

clean-local:

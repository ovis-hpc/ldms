ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
if ENABLE_ETC
SUBDIRS += etc
endif
if ENABLE_SWIG
SUBDIRS += swig
endif

EXTRA_DIST = autogen.sh COPYING doxygen/html

OVIS_LIB = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
OVIS_LIB_TARBALL = $(OVIS_LIB).tar.gz
OPV=-$(PACKAGE_VERSION)

.PHONY: rpm rpm-old toss centos doxygen
rpm-old: dist-gzip
	mkdir -p rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(OVIS_LIB_TARBALL) ovis-lib.spec rpm/SOURCES
	rpmbuild --define "_topdir `pwd`/rpm" -ba ovis-lib.spec

rpm toss centos: dist-gzip
	mkdir -p centos/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(OVIS_LIB_TARBALL) packaging/ovis-lib-toss.spec centos/SOURCES
	rpmbuild --define "_topdir `pwd`/centos" -ba packaging/ovis-lib-toss.spec

rpm-clean:
	rm -rf rpm/ centos/
	rm -f $(OVIS_LIB_TARBALL)

doxygen doxygen/html:
	doxygen ./Doxyfile

if ENABLE_DOC
install-data-local: doxygen
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in README COPYING ChangeLog COPYING; do \
		$(INSTALL_DATA) ${srcdir}/$$f $(DESTDIR)$(docdir)$(OPV); \
	done
	for f in doxygen/html/* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html; \
	done
	for f in doxygen/html/search/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html/search; \
	done
endif

install-exec-local:
	COMMIT=`git rev-parse HEAD`; \
	DIRTY=`git status -uno -s | wc -l`; \
	if [[ $$DIRTY != 0 ]]; then \
		COMMIT="$$COMMIT-dirty" ; \
	fi ; \
	echo '#!/bin/bash' > $(DESTDIR)/$(bindir)/lib-commit-id ; \
	echo echo $$COMMIT >> $(DESTDIR)/$(bindir)/lib-commit-id ; \
	chmod 555 $(DESTDIR)/$(bindir)/lib-commit-id ;

clean-local: rpm-clean

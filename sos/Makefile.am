
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

if ENABLE_SWIG
SUBDIRS += swig
endif

EXTRA_DIST = autogen.sh doxygen/html

OVIS_SOS = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
OVIS_SOS_TARBALL = $(OVIS_SOS).tar.gz
OPV=-$(PACKAGE_VERSION)

.PHONY: rpm centos toss
centos toss rpm: dist-gzip
	mkdir -p centos/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(OVIS_SOS_TARBALL) packaging/ovis-sos.spec centos/SOURCES
	rpmbuild -v --define "_topdir `pwd`/centos" -ba packaging/ovis-sos.spec

doc:
	doxygen Doxyfile

rpm-clean:
	rm -rf rpm/ centos/
	rm -f $(OVIS_SOS_TARBALL)

doxygen doxygen/html:
	doxygen ./Doxyfile

if ENABLE_DOC
install-data-local: doxygen
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in README COPYING ChangeLog ; do \
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
	echo '#!/bin/bash' > $(DESTDIR)/$(bindir)/sos-commit-id ; \
	echo echo $$COMMIT >> $(DESTDIR)/$(bindir)/sos-commit-id ; \
	chmod 555 $(DESTDIR)/$(bindir)/sos-commit-id ;

clean-local: rpm-clean


ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
if ENABLE_ETC
SUBDIRS += etc
endif
LDMS = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
LDMS_TARBALL = $(LDMS).tar.gz
OPV=-$(PACKAGE_VERSION)

.PHONY: rpm centos toss doxygen rpm-old
rpm-old: dist-gzip
	mkdir -p rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(LDMS_TARBALL) rpm/SOURCES
	rpmbuild -ba ldms.spec

rpm toss centos: dist-gzip
	mkdir -p centos/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(LDMS_TARBALL) packaging/ovis-ldms.spec centos/SOURCES
	rpmbuild --define "_topdir `pwd`/centos" -ba packaging/ovis-ldms.spec

rpm-clean:
	rm -rf rpm/ centos
	rm -f $(LDMS_TARBALL)

doc:
	doxygen Doxyfile

doxygen doxygen/html:
	doxygen ./Doxyfile

if ENABLE_DOC
install-data-local: doxygen
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in AUTHORS README COPYING ChangeLog ; do \
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
	echo '#!/bin/bash' > $(DESTDIR)/$(bindir)/ldms-commit-id ; \
	echo echo $$COMMIT >> $(DESTDIR)/$(bindir)/ldms-commit-id ; \
	chmod 555 $(DESTDIR)/$(bindir)/ldms-commit-id ;

clean-local: rpm-clean

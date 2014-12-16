ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
if ENABLE_ETC
SUBDIRS += etc
endif
LDMS = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
LDMS_TARBALL = $(LDMS).tar.gz
OPV=-$(PACKAGE_VERSION)

.PHONY: rpm centos toss doxygen rpm-old doc install-doc
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

if ENABLE_DOC
all: doc
install-data-local: install-doc
endif

doc doxygen doxygen/html:
	doxygen ./Doxyfile

install-doc:
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

PEDIGREE = $(DESTDIR)/$(bindir)/ldms-pedigree
include $(top_srcdir)/../automake/pedigree.am

clean-local: rpm-clean

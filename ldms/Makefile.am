ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
if ENABLE_SWIG
SUBDIRS += swig
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

# Make rule for RPM on RHEL7
rpm7: dist-gzip
	mkdir -p $@/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	ln -f -s ../../automake rpm7/BUILD/automake
	cp $(distdir).tar.gz $@/SOURCES
	cp packaging/rhel7.spec $@/SPECS
	QA_RPATHS=0x0003 ; \
	export QA_RPATHS ; \
	rpmbuild --define "_topdir `pwd`/$@" -ba $@/SPECS/rhel7.spec

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

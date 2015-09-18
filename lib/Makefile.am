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

.PHONY: rpm rpm-old toss centos doxygen doc install-doc
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

.PHONY: rpm7 centos7 rhel7
rpm7 centos7 rhel7: dist-gzip
	mkdir -p rpm7/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	ln -f -s ../../automake rpm7/BUILD/automake
	cp $(OVIS_LIB_TARBALL) rpm7/SOURCES
	cp packaging/ovis-lib-rhel7.spec rpm7/SPECS
	QA_RPATHS=0x0003 ; \
	export QA_RPATHS ; \
	rpmbuild --define "_topdir `pwd`/rpm7" -ba rpm7/SPECS/ovis-lib-rhel7.spec

if ENABLE_DOC
all: doc
install-data-local: install-doc
endif

# The rules docs and install-docs is not under ENABLE_DOC so that users can
# manually call 'make docs' and 'make install-docs'. If
# --enable-docs is given at configure time, 'docs' rule will be invoked when
# "make" is called and 'install-docs' will be invoked when "make install" is
# called.
doc doxygen doxygen/html:
	doxygen ./Doxyfile

install-doc:
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

PEDIGREE = $(DESTDIR)/$(bindir)/lib-pedigree
include $(top_srcdir)/../automake/pedigree.am

clean-local: rpm-clean

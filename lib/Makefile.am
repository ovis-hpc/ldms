ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
if ENABLE_ETC
SUBDIRS += etc
endif
if ENABLE_SWIG
SUBDIRS += swig
endif

EXTRA_DIST = autogen.sh COPYING SHA.txt TAG.txt pedigree.am

OVIS_LIB = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
OVIS_LIB_TARBALL = $(OVIS_LIB).tar.gz
OPV=-$(PACKAGE_VERSION)

PHONY_rpm = rpm rpm-old toss centos doxygen doc install-doc
rpm-old: dist-gzip
	$(MKDIR_P) rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(OVIS_LIB_TARBALL) ovis-lib.spec rpm/SOURCES
	rpmbuild --define "_topdir `pwd`/rpm" -ba ovis-lib.spec

rpm toss centos: dist-gzip
	$(MKDIR_P) centos/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(OVIS_LIB_TARBALL) packaging/ovis-lib-toss.spec centos/SOURCES
	rpmbuild --define "_topdir `pwd`/centos" -ba packaging/ovis-lib-toss.spec

rpm-clean:
	rm -rf rpm/ centos/
	rm -f $(OVIS_LIB_TARBALL)

if ENABLE_DOC
install-data-local: install-doc
endif

if ENABLE_DOC_HTML
DOCDEPS = $(top_srcdir)/src/*/*.c \
          $(top_srcdir)/src/*/*.h \
          $(top_srcdir)/src/*/*/*.h \
          $(top_srcdir)/src/*/*/*.h
all: doxygen-out
doxygen-out: $(DOCDEPS)
	doxygen ./Doxyfile
	touch doxygen-out

.PHONY: install-doxygen $(PHONY_rpm)
install-doxygen: doxygen-out
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in doxygen/html/* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html; \
	done
	if [ -d doxygen/html/search ]; then \
	for f in doxygen/html/search/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html/search; \
	done \
	fi

install-dox: install-doxygen
else
install-dox:
endif

install-doc: install-dox
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)
	for f in README COPYING ChangeLog COPYING; do \
		$(INSTALL_DATA) ${srcdir}/$$f $(DESTDIR)$(docdir)$(OPV); \
	done

PEDIGREE = $(DESTDIR)/$(bindir)/lib-pedigree
include $(srcdir)/pedigree.am

clean-local: rpm-clean

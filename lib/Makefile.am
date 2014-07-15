ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
EXTRA_DIST = autogen.sh COPYRIGHT doxygen/html

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

if ENABLE_DOC
doxygen doxygen/html:
	doxygen ./Doxyfile
	@(errcnt=`wc -l lib.doxygen.log|sed -e s'% .*%%g'`; echo doxygen errors $$errcnt; if [ "$$errcnt" -ge 10 ]; then echo "WOW WE NEED TO CLEAN UP THE COMMENTS!"; fi)

install-data-local: doxygen
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in README COPYING ChangeLog COPYRIGHT; do \
		$(INSTALL_DATA) ${srcdir}/$$f $(DESTDIR)$(docdir)$(OPV); \
	done
	for f in doxygen/html/* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html; \
	done
	for f in doxygen/html/search/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html/search; \
	done
else
doxygen doxygen/html:
	mkdir -p doxygen/html
	echo "Documentation suppressed by configure options" > doxygen/html/README.txt
endif

clean-local: rpm-clean

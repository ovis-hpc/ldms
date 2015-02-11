
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
@ENABLE_SWIG_TRUE@ENABLE_SWIG=1

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

rpm-clean:
	rm -rf rpm/ centos/
	rm -f $(OVIS_SOS_TARBALL)

if ENABLE_DOC
doc:
	doxygen Doxyfile
	@(errcnt=`wc -l sos.doxygen.log|sed -e 's%sos.doxygen.log%%g'`; echo doxygen errors $$errcnt; if [ "$$errcnt" -ge 10 ]; then echo "WOW WE NEED TO CLEAN UP THE COMMENTS!"; fi)


doxygen doxygen/html:
	doxygen ./Doxyfile
	@(errcnt=`wc -l sos.doxygen.log`; echo doxygen errors $$errcnt; if [ "$$errcnt" -ge 10 ]; then echo "WOW WE NEED TO CLEAN UP THE COMMENTS!"; fi)
	

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
else
doxygen doxygen/html:
	mkdir -p doxygen/html
	echo "Documentation suppressed by configure options" > doxygen/html/README.txt
endif

clean-local: rpm-clean

ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

if ENABLE_SCRIPTS
SUBDIRS += scripts
endif
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
	$(MKDIR_P) rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(LDMS_TARBALL) rpm/SOURCES
	rpmbuild -ba ldms.spec

rpm toss centos: dist-gzip
	$(MKDIR_P) centos/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
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

doc:
	doxygen Doxyfile
	@(errcnt=`wc -l ldms.doxygen.log|sed -e s'% .*%%g'`; echo doxygen errors $$errcnt; if [ "$$errcnt" -ge 10 ]; then echo "WOW WE NEED TO CLEAN UP THE COMMENTS!"; fi)

doxygen doxygen/html:
	doxygen ./Doxyfile
	@(errcnt=`wc -l ldms.doxygen.log|sed -e s'% .*%%g'`; echo doxygen errors $$errcnt; if [ "$$errcnt" -ge 10 ]; then echo "WOW WE NEED TO CLEAN UP THE COMMENTS!"; fi)

install-doxygen:
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in doxygen/html/* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html; \
	done
	for f in doxygen/html/search/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html/search; \
	done

else

doc:
	@echo "bypassing doxygen build"

doxygen doxygen/html:
	@echo "no html doc in this version"

install-doxygen:
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)
	@echo "no html doc in this version"

endif

install-data-local: install-doxygen
	for f in AUTHORS README COPYING ChangeLog ; do \
		$(INSTALL_DATA) ${srcdir}/$$f $(DESTDIR)$(docdir)$(OPV); \
	done

PEDIGREE = $(DESTDIR)/$(bindir)/ldms-pedigree
include $(top_srcdir)/../automake/pedigree.am

clean-local: rpm-clean

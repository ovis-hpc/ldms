ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
LDMS = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
LDMS_TARBALL = $(LDMS).tar.gz

.PHONY: rpm
rpm: dist-gzip
	mkdir -p rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(LDMS_TARBALL) rpm/SOURCES
	rpmbuild -ba ldms.spec

rpm-clean:
	rm -rf rpm/
	rm -f $(LDMS_TARBALL)

clean-local: rpm-clean

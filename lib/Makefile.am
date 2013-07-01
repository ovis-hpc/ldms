ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
EXTRA_DIST = autogen.sh

OVIS_LIB = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
OVIS_LIB_TARBALL = $(OVIS_LIB).tar.gz

.PHONY: rpm
rpm: dist-gzip
	mkdir -p rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(OVIS_LIB_TARBALL) rpm/SOURCES
	rpmbuild -ba ovis-lib.spec

rpm-clean:
	rm -rf rpm/
	rm -f $(OVIS_LIB_TARBALL)

clean-local: rpm-clean

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src etc
ME = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
ME_TARBALL = $(ME).tar.gz

.PHONY: rpm
rpm: dist-gzip
	mkdir -p rpm/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(ME_TARBALL) rpm/SOURCES
	rpmbuild -ba me.spec

rpm-clean:
	rm -rf rpm/
	rm -f $(ME_TARBALL)

clean-local: rpm-clean

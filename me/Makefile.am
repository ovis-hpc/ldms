ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src
if ENABLE_ETC
SUBDIRS += etc
endif
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

install-exec-local:
	COMMIT=`git rev-parse HEAD`; \
	DIRTY=`git status -uno -s | wc -l`; \
	if [[ $$DIRTY != 0 ]]; then \
		COMMIT="$$COMMIT-dirty" ; \
	fi ; \
	echo '#!/bin/bash' > $(DESTDIR)/$(bindir)/me-commit-id ; \
	echo echo $$COMMIT >> $(DESTDIR)/$(bindir)/me-commit-id ; \
	chmod 555 $(DESTDIR)/$(bindir)/me-commit-id ;

clean-local: rpm-clean

ACLOCAL_AMFLAGS = -I m4

if ENABLE_SOS
SUBDIRS= lib sos ldms
else
SUBDIRS= lib ldms
endif

BASE = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
BASE_TARBALL = $(BASE).tar.gz
OPV=-$(PACKAGE_VERSION)

.PHONY: rpm centos toss doxygen 

rpm toss centos: dist-gzip
	mkdir -p centos/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(BASE_TARBALL) packaging/ovis-base.spec centos/SOURCES
	rpmbuild --define "_topdir `pwd`/centos" -ba packaging/ovis-base.spec

rpm-clean:
	rm -rf rpm/ centos
	rm -f $(BASE_TARBALL)

clean-local: rpm-clean


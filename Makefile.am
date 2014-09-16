ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST=libevent-2.0.21-stable.tar.gz

## if ENABLE_SOS
## SUBDIRS= lib sos ldms
## else
## SUBDIRS= lib ldms
## endif
SUBDIRS= lib ldms util

BASE = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
BASE_TARBALL = $(BASE).tar.gz
OPV=-$(PACKAGE_VERSION)

.PHONY: rpm centos toss doxygen 

banned:
	(cd ldms; make banned)

rpm centos: dist-gzip
	mkdir -p centos/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(BASE_TARBALL) packaging/ovis-base.spec centos/SOURCES
	rpmbuild --define "_topdir `pwd`/centos" -ba packaging/ovis-base.spec

toss: dist-gzip
	mkdir -p toss/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(BASE_TARBALL) packaging/ovis-base.spec toss/SOURCES
	rpmbuild --define "_topdir `pwd`/toss" -ba packaging/ovis-base.spec

rpm-clean:
	rm -rf rpm/ centos
	rm -f $(BASE_TARBALL)

clean-local: rpm-clean

install-exec-local:
	if test -d libevent-2.0.21-stable/lib/ovis-ldms/lib; then \
		mkdir $(DESTDIR)$(libdir)/ovis-libevent ; \
		cp -a libevent-2.0.21-stable/lib/ovis-ldms/lib/* $(DESTDIR)$(libdir)/ovis-libevent/ ;\
		sed -e 's%LD_LIBRARY_PATH=.*%LD_LIBRARY_PATH=${libdir}/ovis-libevent:$$LD_LIBRARY_PATH%' \
			$(DESTDIR)$(bindir)/*.sh ; \
		/bin/rm -rf $(DESTDIR)$(libdir)/ovis-libevent/pkgconfig ; \
	fi

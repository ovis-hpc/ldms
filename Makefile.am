ACLOCAL_AMFLAGS = -I m4
#
# This top-level make file works when everything is
# built in the required order from the top.
# Manually configuring parts individually and expecting
# the whole to build is obviously difficult.
# Use the top configure if you expect to use the top make.
#
EXTRA_DIST= \
m4/Ovis-top.m4 \
packaging/firerpms \
SHA.txt \
TAG.txt

docdir = $(datadir)/doc/@PACKAGE@-@VERSION@
# dist_doc_DATA = README.txt

SUBDIRS = lib 

# we build of the maybe vars to control build order, which
# AM_CONDITIONAL does not preserve.
# needs lib in properly refactored world
MAYBE_SOS = @MAYBE_SOS@
# needs lib
MAYBE_OCM = @MAYBE_OCM@
# needs lib; optional sos, ocm
MAYBE_LDMS = @MAYBE_LDMS@
# needs lib, sos; optional ocm
MAYBE_BALER = @MAYBE_BALER@
# needs lib; optional ocm
MAYBE_ME = @MAYBE_ME@
# needs lib; optional ocm
MAYBE_KOMONDOR = @MAYBE_KOMONDOR@

SUBDIRS += $(MAYBE_SOS) \
$(MAYBE_OCM) \
$(MAYBE_LDMS) \
$(MAYBE_BALER) \
$(MAYBE_ME) \
$(MAYBE_KOMONDOR) \
util

# this definition of DIST_SUBDIRS is unusual. May decide to improve later.
# For now, we ship as we expect to configure.
# Perhaps all should be included all the time but we surround their
# make content for dist with enable conditionals.
DIST_SUBDIRS = lib sos ldms baler util
# make dist _will_ fail for these unless we mod their .am files
# so not included in dist.
NODIST_SUBDIRS = ocm me komondor

BASE = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
BASE_TARBALL = $(BASE).tar.gz
OPV=-$(PACKAGE_VERSION)

.PHONY: rpm centos toss doxygen lanl-mon6 rhine

banned:
	(cd ldms; make banned)

rpm centos: dist-gzip
	$(MKDIR_P) centos/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(BASE_TARBALL) packaging/ovis-base.spec centos/SOURCES
	rpmbuild --define "_topdir `pwd`/centos" -ba packaging/ovis-base.spec

toss: dist-gzip
	$(MKDIR_P) toss/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(BASE_TARBALL) packaging/ovis-base.spec toss/SOURCES
	rpmbuild --define "_topdir `pwd`/toss" -ba packaging/ovis-base.spec

lanl-mon6: dist-gzip
	$(MKDIR_P) lanl-mon6/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(BASE_TARBALL) packaging/ovis-mon6.spec lanl-mon6/SOURCES
	rpmbuild --define "_topdir `pwd`/lanl-mon6" -ba packaging/ovis-mon6.spec

rhine: dist-gzip
	$(MKDIR_P) rhine/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	cp $(BASE_TARBALL) packaging/ovis-rhine.spec rhine/SOURCES
	rpmbuild --define "_topdir `pwd`/rhine" -ba packaging/ovis-rhine.spec

rpm-clean:
	rm -rf rpm/ centos
	rm -f $(BASE_TARBALL)

clean-local: rpm-clean

install-exec-local:
	if test -d libevent-2.0.21-stable/lib/ovis-ldms/lib; then \
		$(MKDIR_P) $(DESTDIR)$(libdir)/ovis-libevent ; \
		cp -a libevent-2.0.21-stable/lib/ovis-ldms/lib/* $(DESTDIR)$(libdir)/ovis-libevent/ ;\
		sed -i -e 's%LD_LIBRARY_PATH=.*%LD_LIBRARY_PATH=${libdir}/ovis-libevent:$$LD_LIBRARY_PATH%' \
			$(DESTDIR)$(bindir)/*.sh ; \
		/bin/rm -rf $(DESTDIR)$(libdir)/ovis-libevent/pkgconfig ; \
	fi

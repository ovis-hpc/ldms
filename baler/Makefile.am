if ENABLE_BALER

EXTRA_DIST = baler.cfg hosts.txt

ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

if ENABLE_SWIG
SUBDIRS += swig
endif

if ENABLE_ETC
SUBDIRS += etc
endif

if ENABLE_DICT
SUBDIRS += dict
endif

if ENABLE_DOC
SUBDIRS += docs
endif

# Make rule for RPM on RHEL7
rpm7: dist-gzip
	$(MKDIR_P) $@/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
	ln -f -s ../../automake rpm7/BUILD/automake
	cp $(distdir).tar.gz $@/SOURCES
	cp rpm/rhel7.spec $@/SPECS
	QA_RPATHS=0x0003 ; \
	export QA_RPATHS ; \
	rpmbuild --define "_topdir `pwd`/$@" \
		--buildroot `pwd`/$@/BUILDROOT \
		-ba $@/SPECS/rhel7.spec

rpm-clean:
	rm -rf rpm7/
	rm -f $(distdir).tar.gz


endif

PEDIGREE = $(DESTDIR)/$(bindir)/baler-pedigree
include $(top_srcdir)/../automake/pedigree.am

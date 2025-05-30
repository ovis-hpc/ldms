ACLOCAL_AMFLAGS = -I m4
SUBDIRS =
SUBDIRS += third-plugins
SUBDIRS += core
SUBDIRS += ldmsd

if ENABLE_OVIS_AUTH
SUBDIRS += auth
endif

if ENABLE_STORE
SUBDIRS += store
SUBDIRS += decomp
endif

if ENABLE_SAMPLER
SUBDIRS += sampler
endif

if ENABLE_LDMS_TEST
SUBDIRS += test
endif

SUBDIRS += contrib

ac_configure_args=@ac_configure_args@

do_subst = @LDMS_SUBST_RULE@

%.sh: %.sh.in
	$(do_subst) < $< > $@
	chmod 755 $@

ovis-ldms-configure-args:
	-for i in @ac_configure_args@; do \
		(echo -n \'$$i |sed -E 's/(=)(.*)$$/="\2"/' && echo \') | grep -v srcdir | grep '^.--' >> $@; \
	done ; \
	/bin/true

ovis-ldms-configure-env:
	-for i in @ac_configure_args@; do \
		(echo -n $$i |sed -E 's/(=)(.*)$$/="\2"/' | sed -e s/^\'// -e s/\'$$// ) | grep -v '^--' >> $@; \
	done ; \
	/bin/true

# override libdir sanity checks
mylibdir = $(libdir)
mylib_SCRIPTS = ovis-ldms-configvars.sh

EXTRA_DIST = ovis-ldms-configvars.sh.in

# override pkglibdir sanity checks
mypkglibdir = $(pkglibdir)
mypkglib_DATA = ovis-ldms-configure-args ovis-ldms-configure-env

CLEANFILES =  ovis-ldms-configvars.sh ovis-ldms-configure-args ovis-ldms-configure-env

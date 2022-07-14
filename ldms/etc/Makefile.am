ACLOCAL_AMFLAGS = -I m4
docdir = $(datadir)/doc/@PACKAGE@/sample_init_scripts/opt/etc

SUBDIRS =

# init script
#   init.d is currently unmaintained
# SUBDIRS += init.d

# logrotate
#   logrotate.d is currently unmaintained
# SUBDIRS += logrotate.d

# systemd config files
SUBDIRS += systemd

do_subst = @LDMS_SUBST_RULE@

BUILT_FILES = \
	ldmsd.sampler.env \
	ldmsd.aggregator.env \
	ldmsd.kokkos.env \
	papi-sampler.env \
	ldms.plugstack.env
BUILT_FILES_SRC = $(BUILT_FILES:=.in)

EXTRA_DIST=$(BUILT_FILES_SRC)

sysconfldmsdir=$(docdir)/ldms
dist_sysconfldms_DATA= \
	aggregator.conf.cray_xc.example \
	sampler.conf.cray_xc.example \
	ldmsd.aggregator.env.cray_xc.example \
	ldmsd.sampler.env.cray_xc.example \
	ldmsauth.conf \
	ldms.cfg \
	sampler.conf \
	papi-sampler.conf \
	aggregator.conf \
	kokkos.conf
nodist_sysconfldms_DATA=$(BUILT_FILES)

$(builddir)/%.env: $(srcdir)/%.env.in
	$(do_subst) < $< > $@

install-data-hook:
	 chmod 600 $(DESTDIR)$(sysconfldmsdir)/ldmsauth.conf

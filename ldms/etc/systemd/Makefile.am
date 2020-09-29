SYSTEMD_DIR = $(DESTDIR)$(sysconfdir)/systemd/system

do_subst = @LDMS_SUBST_RULE@

BUILT_FILES = \
	ldmsd.sampler.service \
	ldmsd.aggregator.service \
	ldmsd.kokkos.service \
	papi-sampler.service
BUILT_FILES_SRC = $(BUILT_FILES:=.in)

EXTRA_DIST = $(BUILT_FILES_SRC)

sysconfsystemdsystemdir=$(sysconfdir)/systemd/system
nodist_sysconfsystemdsystem_DATA=$(BUILT_FILES)

$(builddir)/%.service: $(srcdir)/%.service.in
	$(do_subst) < $< > $@

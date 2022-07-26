docdir = $(datadir)/doc/@PACKAGE@/sample_init_scripts/opt/etc

do_subst = @LDMS_SUBST_RULE@

set-ovis-variables.sh: set-ovis-variables.sh.in
	$(do_subst) < $< > $@

sysconfovisdir = $(docdir)/ovis
dist_sysconfovis_DATA = ovis-functions.sh

sysconfprofiledir = $(docdir)/profile.d
sysconfprofile_DATA = set-ovis-variables.sh

if SYSCONFDIR_NOT_ETC
sysconfprofileddir=$(sysconfdir)/profile.d
sysconfprofiled_DATA=set-ovis-variables.sh
endif

EXTRA_DIST = set-ovis-variables.sh.in


do_subst = @LDMS_SUBST_RULE@

set-ovis-variables.sh: set-ovis-variables.sh.in
	$(do_subst) < $< > $@

sysconfovisdir = $(sysconfdir)/ovis
dist_sysconfovis_DATA = ovis-functions.sh

sysconfprofiledir = $(sysconfdir)/profile.d
sysconfprofile_DATA = set-ovis-variables.sh

EXTRA_DIST = set-ovis-variables.sh.in


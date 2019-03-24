ACLOCAL_AMFLAGS = -I m4
SUBDIRS =
EXTRA_DIST=
CLEANFILES =
DISTCLEANFILES =

do_subst = @LDMS_SUBST_RULE@

%.sh: %.sh.in
	$(do_subst) < $< > $@
	chmod 755 $@

if ENABLE_IPMIREADER
SUBDIRS += ipmireader
endif

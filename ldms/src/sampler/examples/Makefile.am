ACLOCAL_AMFLAGS = -I m4
SUBDIRS =
EXTRA_DIST=
CLEANFILES =
DISTCLEANFILES =

do_subst = @LDMS_SUBST_RULE@

%.sh: %.sh.in
	$(do_subst) < $< > $@
	chmod 755 $@

if ENABLE_ARRAY_EXAMPLE
SUBDIRS += array_example
endif

if ENABLE_SYNTHETIC
SUBDIRS += synthetic
endif

if ENABLE_TEST_SAMPLER_LDMS_TEST
SUBDIRS += test_sampler
endif

if ENABLE_LIST_SAMPLER_LDMS_TEST
SUBDIRS += list_sampler
endif

if ENABLE_RECORD_SAMPLER_LDMS_TEST
SUBDIRS += record_sampler
endif

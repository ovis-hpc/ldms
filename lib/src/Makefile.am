EXTRA_DIST=ovis-lib-configvars.sh.in ovis-test/test.h
CLEANFILES = ovis-lib-configvars.sh
DISTCLEANFILES = $(nobase_include_HEADERS)

do_subst = @LDMS_SUBST_RULE@

%.sh: %.sh.in
	$(do_subst) < $< > $@
	chmod 755 $@

SUBDIRS = third

if ENABLE_OVIS_UTIL
SUBDIRS += ovis_util
endif

if ENABLE_COLL
SUBDIRS += coll
endif

if ENABLE_MMALLOC
SUBDIRS += mmalloc
endif

if ENABLE_OVIS_AUTH
SUBDIRS += ovis_auth
endif

if ENABLE_OVIS_EVENT
SUBDIRS += ovis_event
endif

if ENABLE_OVIS_EV
SUBDIRS += ovis_ev
endif

SUBDIRS += ovis_ref

if ENABLE_ZAP
SUBDIRS += zap
endif

if ENABLE_OVIS_CTRL
SUBDIRS += ovis_ctrl
endif

SUBDIRS += ovis_json


# override libdir sanity checks
mylibdir = $(libdir)
mylib_SCRIPTS = ovis-lib-configvars.sh

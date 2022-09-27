EXTRA_DIST=ovis-lib-configvars.sh.in ovis-test/test.h
CLEANFILES = ovis-lib-configvars.sh
DISTCLEANFILES = $(nobase_include_HEADERS)

do_subst = @LDMS_SUBST_RULE@

%.sh: %.sh.in
	$(do_subst) < $< > $@
	chmod 755 $@

SUBDIRS = third

SUBDIRS += ovis_util
SUBDIRS += coll
SUBDIRS += ovis_ev
SUBDIRS += ovis_log

if ENABLE_MMALLOC
SUBDIRS += mmalloc
endif

if ENABLE_OVIS_AUTH
SUBDIRS += ovis_auth
endif

if ENABLE_OVIS_EVENT
SUBDIRS += ovis_event
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

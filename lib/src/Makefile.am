nobase_include_HEADERS = ovis-test/test.h ovis-lib-config.h
EXTRADIST=ovis-lib-configvars.sh.in

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

if ENABLE_ZAP
SUBDIRS += zap
endif

if ENABLE_OVIS_CTRL
SUBDIRS += ovis_ctrl
endif

if ENABLE_OVIS_EVENT
SUBDIRS += ovis_event
endif

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(libdir)
	sed -i -e 's/{//g' -e 's/}//g' ovis-lib-configvars.sh
	$(INSTALL) -m 644 ovis-lib-configvars.sh $(DESTDIR)$(libdir)

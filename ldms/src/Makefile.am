nobase_include_HEADERS = ovis-ldms-config.h
ACLOCAL_AMFLAGS = -I m4
SUBDIRS =
EXTRADIST=ovis-ldms-configvars.sh.in

if ENABLE_CORE
SUBDIRS += core
endif

if ENABLE_STORE
SUBDIRS += store
endif

if ENABLE_SAMPLER
SUBDIRS += sampler
endif

if ENABLE_LDMSD
SUBDIRS += ldmsd
endif

if ENABLE_LDMS_TEST
SUBDIRS += test
endif

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(libdir)
	sed -i -e 's/{//g' -e 's/}//g' ovis-ldms-configvars.sh
	$(INSTALL) -m 644 ovis-ldms-configvars.sh $(DESTDIR)$(libdir)


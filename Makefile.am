ACLOCAL_AMFLAGS = -I m4
#
# This top-level make file works when everything is
# built in the required order from the top.
# Manually configuring parts individually and expecting
# the whole to build is obviously difficult.
# Use the top configure if you expect to use the top make.
#
EXTRA_DIST= \
autogen.sh \
m4/Ovis-top.m4 \
m4/gnulib-cache.m4 \
SHA.txt \
README.md

EXTRA_DIST += $(top_srcdir)/.version
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

DISTCLEANFILES = ovis-ldms-config.h
nobase_include_HEADERS = ovis-ldms-config.h



docdir = $(datadir)/doc/@PACKAGE@
dist_doc_DATA = COPYING gpl-2.0.txt

SUBDIRS = lib ldms util gnulib

# this definition of DIST_SUBDIRS is unusual. May decide to improve later.
# For now, we ship as we expect to configure.
# Perhaps all should be included all the time but we surround their
# make content for dist with enable conditionals.
DIST_SUBDIRS = lib ldms util gnulib
# make dist _will_ fail for these unless we mod their .am files
# so not included in dist.
NODIST_SUBDIRS =

BASE = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
BASE_TARBALL = $(BASE).tar.gz

.PHONY: doxygen

banned:
	(cd ldms; make banned)

install-data-hook:
if ENABLE_NOLA
	$(abs_top_srcdir)/util/nola.sh $(DESTDIR)$(libdir) > nola.log
endif
if ENABLE_THIRD_PLUGINS
	@echo "*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#"
	@if test -n "@THIRD_PLUGIN_DIRS@"; then \
		echo "BUILDING third-plugins IN ldms"; \
		(cd ldms/src/third-plugins && make third-install-hook); \
		echo "third-plugins BUILD COMPLETED."; \
	fi
if ENABLE_NOLA
	$(abs_top_srcdir)/util/nola.sh $(DESTDIR)$(libdir) > nola.third-plugins.log
endif
endif

SHA.txt:
	COMMIT=`git rev-parse HEAD 2>/dev/null`; \
	DIRTY=`git status -uno -s 2>/dev/null`; \
	if test -z "$$COMMIT"; then \
		COMMIT="NO_GIT"; \
	elif test -n "$$DIRTY"; then \
		COMMIT=$${COMMIT}-dirty; \
	fi ; \
	echo "$$COMMIT" > SHA.txt

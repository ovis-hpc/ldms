
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

if ENABLE_SCRIPTS
SUBDIRS += scripts
endif
if ENABLE_ETC
SUBDIRS += etc
endif

if ENABLE_PYTHON
SUBDIRS += python
endif

LDMS = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
LDMS_TARBALL = $(LDMS).tar.gz

PHONY_doc = doxygen doc install-doc

EXTRA_DIST = \
test/failover-delay/agg11.sh \
test/failover-delay/agg12.sh \
test/failover-delay/env.sh \
test/failover-delay/sampler1.sh \
test/failover-delay/sampler2.sh \
test/failover-simple/agg1.sh \
test/failover-simple/agg2.sh \
test/failover-simple/sampler.sh

docversiondir=$(docdir)
docversion_DATA=AUTHORS

install-exec-hook:
	@echo "*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#"
	@echo "INSTALL COMPLETED."
if ENABLE_DEVELOPER
	@echo "Errors seen after this matter to plugin or core developers only."
	@echo "*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#"
	-$(bindir)/ldms_ban.sh $(srcdir)/../

endif

banned:
	bash $(srcdir)/scripts/ldms_ban.sh.in $(srcdir)/../

if ENABLE_DOC

PHONY_dox = install-dox

if ENABLE_DOC_HTML
DOCDEPS = $(top_srcdir)/ldms/src/*/*.c \
          $(top_srcdir)/ldms/src/*/*.h \
          $(top_srcdir)/ldms/src/*/*/*.h \
          $(top_srcdir)/ldms/src/*/*/*.h
all: doxygen-out
doxygen-out: $(DOCDEPS)
	doxygen Doxyfile
	touch doxygen-out
	@(errcnt=`wc -l ldms.doxygen.log|sed -e s'% .*%%g'`; echo doxygen errors $$errcnt; if [ "$$errcnt" -ge 10 ]; then echo "WOW WE NEED TO CLEAN UP THE COMMENTS!"; fi)

install-doxygen: doxygen-out
	$(MKDIR_P) $(DESTDIR)$(docdir)/html/search
	for f in doxygen/html/* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)/html; \
	done
	if [ -d doxygen/html/search ]; then \
	for f in doxygen/html/search/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)/html/search; \
	done \
	fi

install-dox: install-doxygen
endif
else
install-dox:
endif

if ENABLE_DOC_MAN
SUBDIRS += man
endif

.PHONY: $(PHONY_doc) $(PHONY_dox)

install-data-local: install-dox

PEDIGREE = $(DESTDIR)/$(bindir)/ldms-pedigree

install-exec-local:
	mkdir -p `dirname $(PEDIGREE)` ; \
	if [ -f $(PEDIGREE) ]; then \
		rm -f $(PEDIGREE); \
	fi ; \
	echo '#!/bin/bash' > $(PEDIGREE) ; \
	echo echo commit-id: "$(OVIS_GIT_LONG)" >> $(PEDIGREE) ; \
	echo echo package: "$(PACKAGE_STRING)" >> $(PEDIGREE) ; \
	echo echo questions to: "$(PACKAGE_BUGREPORT)" >> $(PEDIGREE) ; \
	echo echo source location: "$(abs_top_srcdir)" >> $(PEDIGREE) ; \
	echo echo build location: "$(abs_top_builddir)" >> $(PEDIGREE) ; \
	echo echo compile hostname: "$$HOSTNAME" >> $(PEDIGREE) ; \
	echo echo compile cpu: "`uname -p`" >> $(PEDIGREE) ; \
	echo echo compile os: "`uname -o`" >> $(PEDIGREE) ; \
	echo echo configure args: "\"$(ac_configure_args)\"" >> $(PEDIGREE) ; \
	echo 'if [ "$$1" == "--stat" ] ; then ' >> $(PEDIGREE) ; \
	echo echo "------ git status ------" >> $(PEDIGREE) ; \
	echo echo "\"`cd $(abs_top_srcdir) && git status -suno .`\"" >> $(PEDIGREE) ; \
	echo echo "------------------------" >> $(PEDIGREE) ; \
	echo fi >> $(PEDIGREE); \
	chmod 755 $(PEDIGREE) ;

SHA.txt:
	echo "$(OVIS_GIT_LONG)" > SHA.txt

uninstall-hook:
	rm $(PEDIGREE)

clean-local:

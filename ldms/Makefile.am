EXTRA_DIST = pedigree.am SHA.txt TAG.txt

ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

if ENABLE_SCRIPTS
SUBDIRS += scripts
endif
if ENABLE_SWIG
SUBDIRS += swig
endif
if ENABLE_ETC
SUBDIRS += etc
endif

if ENABLE_LDMS_PYTHON
SUBDIRS += python
endif

LDMS = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
LDMS_TARBALL = $(LDMS).tar.gz
OPV=-$(PACKAGE_VERSION)

PHONY_doc = doxygen doc install-doc

if ENABLE_DEVELOPER

install-exec-hook:
	@echo "*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#"
	@echo "INSTALL COMPLETED."
	@echo "Errors seen after this matter to plugin or core developers only."
	@echo "*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#"
	-$(bindir)/ldms_ban.sh $(srcdir)/../

endif

banned:
	bash $(srcdir)/scripts/ldms_ban.sh.in $(srcdir)/../

if ENABLE_DOC

PHONY_dox = install-dox

if ENABLE_DOC_HTML
DOCDEPS = $(top_srcdir)/src/*/*.c \
          $(top_srcdir)/src/*/*.h \
          $(top_srcdir)/src/*/*/*.h \
          $(top_srcdir)/src/*/*/*.h
all: doxygen-out
doxygen-out: $(DOCDEPS)
	doxygen Doxyfile
	touch doxygen-out
	@(errcnt=`wc -l ldms.doxygen.log|sed -e s'% .*%%g'`; echo doxygen errors $$errcnt; if [ "$$errcnt" -ge 10 ]; then echo "WOW WE NEED TO CLEAN UP THE COMMENTS!"; fi)

install-doxygen: doxygen-out
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in doxygen/html/* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html; \
	done
	if [ -d doxygen/html/search ]; then \
	for f in doxygen/html/search/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html/search; \
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
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)
	for f in AUTHORS README COPYING ChangeLog ; do \
		$(INSTALL_DATA) ${srcdir}/$$f $(DESTDIR)$(docdir)$(OPV); \
	done

PEDIGREE = $(DESTDIR)/$(bindir)/ldms-pedigree
include $(top_srcdir)/pedigree.am

uninstall-hook:
	rm $(PEDIGREE)
	for f in AUTHORS README COPYING ChangeLog ; do \
		rm $(DESTDIR)$(docdir)$(OPV)/$$f; \
	done

clean-local:

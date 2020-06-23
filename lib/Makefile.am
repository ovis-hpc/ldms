ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

if ENABLE_ETC
SUBDIRS += etc
endif

if ENABLE_SWIG
SUBDIRS += swig
endif

if ENABLE_LIB_PYMAP
SUBDIRS += python
endif

EXTRA_DIST = 

OPV=-$(PACKAGE_VERSION)

PHONY_doc = doxygen doc install-doc

if ENABLE_DOC
install-data-local: install-doc
endif

if ENABLE_DOC_HTML
DOCDEPS = $(top_srcdir)/lib/src/*/*.c \
          $(top_srcdir)/lib/src/*/*.h \
          $(top_srcdir)/lib/src/*/*/*.h \
          $(top_srcdir)/lib/src/*/*/*.h
all: doxygen-out
doxygen-out: $(DOCDEPS)
	doxygen ./Doxyfile
	touch doxygen-out

.PHONY: install-doxygen $(PHONY_doc)
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
else
install-dox:
endif

install-doc: install-dox
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)
	for f in README COPYING ChangeLog COPYING; do \
		$(INSTALL_DATA) ${srcdir}/$$f $(DESTDIR)$(docdir)$(OPV); \
	done

clean-local:

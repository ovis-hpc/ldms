if HAVE_DOXYGEN

.PHONY: doc docs
all: doc
doc: docs

docs: doxygen-out

DOCDEPS = $(shell find $(top_srcdir)/src -iname '*.[ch]')

doxygen-out: $(DOCDEPS)
	@DOXYGEN@
	touch doxygen-out

.PHONY: install-doc-dir
install-doc-dir:
	mkdir -p $(DESTDIR)$(docdir)

if ENABLE_DOC_LATEX
.PHONY: install-doc-latex uninstall-doc-latex
docs: latex/refman.pdf

DEST_PDF = $(DESTDIR)$(docdir)/$(PACKAGE)-$(VERSION).pdf

latex/refman.pdf: doxygen-out
	$(MAKE) -C latex

install-doc-latex: install-doc-dir latex/refman.pdf
	cp latex/refman.pdf $(DEST_PDF)

install-data-local: install-doc-latex

uninstall-doc-latex:
	rm -f $(DEST_PDF)

uninstall-local: uninstall-doc-latex
endif

if ENABLE_DOC_HTML
.PHONY: install-doc-html uninstall-doc-html
install-doc-html: install-doc-dir doxygen-out
	cp -r html $(DESTDIR)$(docdir)/

install-data-local: install-doc-html

uninstall-doc-html:
	rm -rf $(DESTDIR)$(docdir)/html

uninstall-local: uninstall-doc-html
endif

if ENABLE_DOC_MAN

man_files = man/man3/balerd.3 \
	    man/man3/bhttpd.3 \
	    man/man3/bquery.3

$(man_files): doxygen-out

man3_MANS = $(man_files)

endif

clean-local:
	rm -rf html latex man doxygen-out

else

doc:
	@echo "doxygen not found!!!"
	@exit -1

endif

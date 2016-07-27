if HAVE_DOXYGEN

.PHONY: doc docs
all: doc
doc: docs

docs: doxygen-out texdocs

#DOCDEPS = $(shell find $(top_srcdir)/src -iname '*.[ch]')
DOCDEPS = $(top_srcdir)/src/*/*.c $(top_srcdir)/src/*/*.h

doxygen-out: $(DOCDEPS)
	@DOXYGEN@
	touch doxygen-out

PHONY_docdir = install-doc-dir
install-doc-dir:
	$(MKDIR_P) $(DESTDIR)$(docdir)

if ENABLE_DOC_LATEX
PHONY_tex = install-doc-latex uninstall-doc-latex
texdocs: latex/refman.pdf

DEST_PDF = $(DESTDIR)$(docdir)/$(PACKAGE)-$(VERSION).pdf

latex/refman.pdf: doxygen-out
	$(MAKE) -C latex

install-doc-latex: install-doc-dir latex/refman.pdf
	cp latex/refman.pdf $(DEST_PDF)

install-data-local: install-doc-latex

uninstall-doc-latex:
	rm -f $(DEST_PDF)

uninstall-local: uninstall-doc-latex
else

texdocs:

endif

if ENABLE_DOC_HTML
PHONY_html = install-doc-html uninstall-doc-html
install-doc-html: install-doc-dir doxygen-out
	cp -r html $(DESTDIR)$(docdir)/

install-data-local: install-doc-html

uninstall-doc-html:
	rm -rf $(DESTDIR)$(docdir)/html

uninstall-local: uninstall-doc-html
endif

if ENABLE_DOC_MAN

# generate man1 from Doxygen man3 output.
man/man1/%.1: man/man3/%.3
	mkdir -p man/man1
	sed -r 's/^(.TH) ([^ ]+) 3 (.*)/\1 \2 1 \3/' $< > $@

man/man5/%.5: man/man3/%.3
	mkdir -p man/man5
	sed -r 's/^(.TH) ([^ ]+) 3 (.*)/\1 \2 5 \3/' $< > $@

# Doxygen generate only one section (default: 3), which makes sense since a lot
# of documentation is development documentation.
#
# man1_files will use the rule above to convert man3 to man1.
man1_files = man/man1/balerd.1 \
	    man/man1/bhttpd.1 \
	    man/man1/bquery.1 \
	    man/man1/bassoc.1

$(man1_files): doxygen-out

man1_MANS = $(man1_files)


# Plugin configuration documents are put in section 5 (config files)
man5_files = man/man5/bin_rsyslog_tcp.config.5 \
	     man/man5/bin_metric.config.5 \
	     man/man5/bout_sos_msg.config.5 \
	     man/man5/bout_sos_img.config.5

$(man5_files): doxygen-out

man5_MANS = $(man5_files)

endif

clean-local:
	rm -rf html latex man doxygen-out

else

doc:
	@echo "doxygen not found!!!"
	@exit -1

.PHONY: $(PHONY_docdir) $(PHONY_tex) $(PHONY_html)

endif

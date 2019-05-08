DOCDEPS = $(top_srcdir)/src/*/*.c \
          $(top_srcdir)/src/*/*.h \
          $(top_srcdir)/src/*/*/*.h \
          $(top_srcdir)/src/*/*/*.h \
          $(top_srcdir)/src/*/*.md \
          $(top_srcdir)/src/*/*/*.md \
	  $(top_srcdir)/README.md \
	  $(top_srcdir)/doc/mdtest.md

EXTRA_DIST = Doxyfile.in mdtest.md mddoxymanfix.sh

empty =

OPV=-$(PACKAGE_VERSION)

.PHONY = install-doxygen-html
TARGETS =

all: doxygen-out

doxygen-out: $(DOCDEPS) Makefile.am Doxyfile
	rm -f ldms
	ln -s $(top_srcdir) ldms
	doxygen $(abs_builddir)/Doxyfile
	touch doxygen-out
	@(\
	 warncnt=$$(cat ldms.doxygen.log 2>/dev/null | grep -c 'warning:'); \
	       if [ "$$warncnt" -gt 0 ]; then \
		       echo "***** $$warncnt Doxygen WARNINGS! *****"; \
		       echo "--- See ldms.doxygen.log for details ---"; \
	       fi\
	)

install-doxygen-html: doxygen-out
	$(MKDIR_P) $(DESTDIR)$(docdir)$(OPV)/html/search
	for f in doxygen/html/* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html; \
	done
	if [ -d doxygen/html/search ]; then \
	for f in doxygen/html/search/*; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)$(OPV)/html/search; \
	done \
	fi

if ENABLE_DOC_HTML
TARGETS += install-doxygen-html
endif


if ENABLE_DOC_MAN

doxygen/man/man3/%.3: doxygen-out
	@( true ) # need dummy recipe

# programs
man1_MANS =

# Library / API
man3_MANS =

# File format (e.g. config file)
man5_MANS =

# MISC
man7_MANS = man/ldmsd-sampler.7 \
	    man/ldmsd-aggregator.7 \
	    man/ldmsd-sampler-dev.7 \
	    man/ldmsd-store-dev.7 \
	    man/mdtest.7

man/ldmsd-sampler.7: doxygen/man/man3/md_ldms_src_ldmsd_ldmsd-sampler.3
man/ldmsd-aggregator.7: doxygen/man/man3/md_ldms_src_ldmsd_ldmsd-aggregator.3
man/ldmsd-sampler-dev.7: doxygen/man/man3/md_ldms_src_ldmsd_ldmsd-sampler-dev.3
man/ldmsd-store-dev.7: doxygen/man/man3/md_ldms_src_ldmsd_ldmsd-store-dev.3
man/mdtest.7: doxygen/man/man3/md_ldms_doc_mdtest.3

# Sys admin commands
man8_MANS =

DOX_MAN = $(man1_MANS) $(man3_MANS) $(man5_MANS) $(man7_MANS) $(man8_MANS)

$(DOX_MAN):
	mkdir -p man/
	SECTION=$(subst .,,$(suffix $(@))) \
	HEADER="LDMS Manuals" \
		$(srcdir)/mddoxymanfix.sh $^ > $@

endif

clean:
	rm -rf doxygen/ doxygen-out ldms.doxygen.log man/


install-data-local: $(TARGETS)

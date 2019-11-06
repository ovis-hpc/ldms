
bin_SCRIPTS= 

PLUGIN_NAME=my_plugin

install-serial:
	$(MKDIR_P) $(DESTDIR)$(docdir)/examples/static-test && \
	uf=$(DESTDIR)$(docdir)/examples/static-test/.uninst.ldms && \
	touch $$uf && \
	for f in $(srcdir)/$(PLUGIN_NAME)* ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)$(docdir)/examples/static-test; \
		echo $$(basename $$f) >> $$uf; \
	done && \
	echo my_plugin >> $(DESTDIR)$(docdir)/examples/static-test/.canned

all-local: always_check

install-data-local: install-serial

always_check:
	@echo checking for banned libs that foul up plugin libtool linkage
	@../scripts/check_banned.sh $(NO_BAN)

rm_banned:
	../scripts/rm_banned.sh

.PHONY: always_check

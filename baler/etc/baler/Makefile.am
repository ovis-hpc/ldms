ACLOCAL_AMFLAGS = -I m4

CONF_DIR = $(DESTDIR)$(sysconfdir)/baler

do_subst = sed -e 's,[@]datadir[@],$(datadir),g' \
	   -e 's,[@]localstatedir[@],$(localstatedir),g' \
	   -e 's,[@]sysconfdir[@],$(sysconfdir),g' \
	   -e 's,[@]sharedstatedir[@],$(sharedstatedir),g' \
	   -e 's,[@]ZAP_LIBPATH[@],$(ZAP_LIBPATH),g' \
	   -e 's,[@]sbindir[@],$(sbindir),g'

EXTRA_DIST = balerd.master.conf.in \
	     balerd.master.env.in \
	     balerd.slave.conf.in \
	     balerd.slave.env.in \
	     bhttpd.env.in \
	     hosts.txt

ENV_FILES = balerd.master.env \
	    balerd.slave.env \
	    bhttpd.env

CONF_FILES = balerd.master.conf \
	     balerd.slave.conf

# rules to generate env file
%.env: $(srcdir)/%.env.in
	$(do_subst) < $< > $@

# rules for conf file
%.conf: $(srcdir)/%.conf.in
	$(do_subst) < $< > $@

install-data-local: $(ENV_FILES) $(CONF_FILES)
	$(MKDIR_P) $(CONF_DIR)
	$(INSTALL_DATA) $(ENV_FILES) $(CONF_FILES) $(CONF_DIR)
	$(INSTALL_DATA) $(srcdir)/hosts.txt $(CONF_DIR)

uninstall-local:
	cd $(CONF_DIR) && rm -f $(ENV_FILES) $(CONF_FILES) hosts.txt

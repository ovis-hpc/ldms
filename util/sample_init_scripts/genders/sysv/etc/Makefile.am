
docdir = $(datadir)/doc/@PACKAGE@-@VERSION@/sample_init_scripts/genders/sysv/etc

if ENABLE_TOSS_SYSTEMD

SUBDIRS = init.d

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(docdir)
	(cd $(DESTDIR)$(docdir); \
		if ! test -L ./sysconfig; then \
			if test -d ./sysconfig; then \
				back=`mktemp -u ./sysconfig.bak.XXX`; \
				mv ./sysconfig $$back; \
			fi; \
			ln -s ../../systemd/etc/sysconfig . ;\
		fi; \
	)
endif

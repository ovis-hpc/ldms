
docdir = $(datadir)/doc/@PACKAGE@-@VERSION@/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/ClusterSecrets

if ENABLE_TOSS_SYSTEMD
dist_doc_DATA = \
README ldmsauth.conf
endif

install-data-hook:
	if test -f $(docdir)/ldmsauth.conf; then \
		chmod 600 $(docdir)/ldmsauth.conf; \
	fi

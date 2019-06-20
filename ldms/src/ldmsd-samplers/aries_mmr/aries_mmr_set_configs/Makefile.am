EXTRA_DIST = \
	metric_set_nic \
	metric_set_rtr_0 \
	metric_set_rtr_0_1_c \
	metric_set_rtr_0_1_s \
	metric_set_rtr_0_2_c \
	metric_set_rtr_0_2_s \
	metric_set_rtr_1 \
	metric_set_rtr_1_1_c \
	metric_set_rtr_1_1_s \
	metric_set_rtr_1_2_c \
	metric_set_rtr_1_2_s \
	metric_set_rtr_2 \
	metric_set_rtr_2_1_c \
	metric_set_rtr_2_2_c \
	metric_set_rtr_2_2_s \
	metric_set_rtr_3 \
	metric_set_rtr_3_1_c \
	metric_set_rtr_3_2_c \
	metric_set_rtr_3_2_s

MMR_FILES = \
	$(srcdir)/metric_set_nic \
	$(srcdir)/metric_set_rtr_0 \
	$(srcdir)/metric_set_rtr_0_1_c \
	$(srcdir)/metric_set_rtr_0_1_s \
	$(srcdir)/metric_set_rtr_0_2_c \
	$(srcdir)/metric_set_rtr_0_2_s \
	$(srcdir)/metric_set_rtr_1 \
	$(srcdir)/metric_set_rtr_1_1_c \
	$(srcdir)/metric_set_rtr_1_1_s \
	$(srcdir)/metric_set_rtr_1_2_c \
	$(srcdir)/metric_set_rtr_1_2_s \
	$(srcdir)/metric_set_rtr_2 \
	$(srcdir)/metric_set_rtr_2_1_c \
	$(srcdir)/metric_set_rtr_2_2_c \
	$(srcdir)/metric_set_rtr_2_2_s \
	$(srcdir)/metric_set_rtr_3 \
	$(srcdir)/metric_set_rtr_3_1_c \
	$(srcdir)/metric_set_rtr_3_2_c \
	$(srcdir)/metric_set_rtr_3_2_s

MMR_DIR = $(DESTDIR)$(sysconfdir)/ldms/aries_mmr_set_configs
install-data-local: $(MMR_FILES)
	$(MKDIR_P) $(MMR_DIR)
	$(INSTALL_DATA) $(MMR_FILES) $(MMR_DIR)/

uninstall-local:
	rm -rf $(MMR_DIR)

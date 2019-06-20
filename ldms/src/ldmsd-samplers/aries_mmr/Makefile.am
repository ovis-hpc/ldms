include ../common.am

pkglib_LTLIBRARIES = libaries_mmr.la
libaries_mmr_la_SOURCES = aries_mmr.c aries_mmr.h
libaries_mmr_la_CFLAGS = $(SAMPLER_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR)
libaries_mmr_la_LIBADD = $(SAMPLER_LIBADD) -lgpcd
libaries_mmr_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@ \
			  -L@ARIES_LIBGPCD_LIBDIR@

pkglib_LTLIBRARIES += libaries_rtr_mmr.la
libaries_rtr_mmr_la_SOURCES = aries_rtr_mmr.c aries_mmr.h
libaries_rtr_mmr_la_CFLAGS = $(SAMPLER_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR)
libaries_rtr_mmr_la_LIBADD = $(SAMPLER_LIBADD) -lgpcd ./libaries_mmr.la
libaries_rtr_mmr_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@ \
			      -L@ARIES_LIBGPCD_LIBDIR@

pkglib_LTLIBRARIES += libaries_nic_mmr.la
libaries_nic_mmr_la_SOURCES = aries_nic_mmr.c aries_mmr.h
libaries_nic_mmr_la_CFLAGS = $(SAMPLER_CFLAGS) -I$(ARIES_LIBGPCD_INCDIR)
libaries_nic_mmr_la_LIBADD = $(SAMPLER_LIBADD) -lgpcd ./libaries_mmr.la
libaries_nic_mmr_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@ \
			      -L@ARIES_LIBGPCD_LIBDIR@

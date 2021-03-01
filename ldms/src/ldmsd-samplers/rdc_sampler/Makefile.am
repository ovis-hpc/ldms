include ../common.am

pkglib_LTLIBRARIES = librdc_sampler.la

librdc_sampler_la_SOURCES = rdc_sampler.c
librdc_sampler_la_CFLAGS  = $(SAMPLER_CFLAGS) @ROCM_RDC_INC_INCDIR_FLAG@
librdc_sampler_la_LIBADD  = $(SAMPLER_LIBADD) -lrdc_bootstrap
librdc_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS) @ROCM_RDC_INC_LIBDIR_FLAG@ @ROCM_SMI_INC_LIBDIR_FLAG@


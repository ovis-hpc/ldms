include ../common.am

pkglib_LTLIBRARIES = libslurm_sampler.la

libslurm_sampler_la_SOURCES = slurm_sampler.c slurm_sampler.h
libslurm_sampler_la_CFLAGS = $(SAMPLER_CFLAGS)
libslurm_sampler_la_LIBADD = $(SAMPLER_LIBADD) -ljson_util
libslurm_sampler_la_LDFLAGS = $(SAMPLER_LDFLAGS)

if ENABLE_SPANK_PLUGIN
SUBDIRS = spank
endif

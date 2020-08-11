include ../common.am

pkglib_LTLIBRARIES = libjobinfo.la

libjobinfo_la_SOURCES = jobinfo.c jobinfo.h
libjobinfo_la_CFLAGS  = $(SAMPLER_CFLAGS)
libjobinfo_la_LIBADD  = $(SAMPLER_LIBADD) \
			$(top_builddir)/lib/src/ovis_util/libovis_util.la \
			$(top_builddir)/lib/src/coll/libcoll.la \
			-lpthread
libjobinfo_la_LDFLAGS = $(SAMPLER_LDFLAGS) @LDFLAGS_GETTIME@

if ENABLE_SLURM_JOBINFO
SUBDIRS = jobinfo_slurm/
endif

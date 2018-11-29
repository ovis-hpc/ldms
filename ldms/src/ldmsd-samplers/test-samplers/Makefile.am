include ../common.am

pkglib_LTLIBRARIES =

pkglib_LTLIBRARIES += libfaux_job.la
libfaux_job_la_SOURCES = faux_job.c
libfaux_job_la_CFLAGS = $(SAMPLER_CFLAGS)
libfaux_job_la_LIBADD = $(SAMPLER_LIBADD)
libfaux_job_la_LDFLAGS = $(SAMPLER_LDFLAGS)

pkglib_LTLIBRARIES += libclock.la
libclock_la_SOURCES = clock.c
libclock_la_CFLAGS = $(SAMPLER_CFLAGS)
libclock_la_LIBADD = $(SAMPLER_LIBADD)
libclock_la_LDFLAGS = $(SAMPLER_LDFLAGS)

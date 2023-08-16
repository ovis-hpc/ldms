pkglib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = $(top_builddir)/ldms/src/sampler/libsampler_base.la \
		$(top_builddir)/ldms/src/core/libldms.la \
		@LDFLAGS_GETTIME@ \
		$(top_builddir)/lib/src/ovis_util/libovis_util.la \
		$(top_builddir)/lib/src/coll/libcoll.la


libtsampler_la_SOURCES = tsampler.c tsampler.h
libtsampler_la_LIBADD = $(COMMON_LIBADD) \
                        $(top_builddir)/lib/src/ovis_event/libovis_event.la -lpthread
pkglib_LTLIBRARIES += libtsampler.la

libtimer_base_la_SOURCES = timer_base.c timer_base.h
libtimer_base_la_LIBADD = $(COMMON_LIBADD) libtsampler.la $(JOBID_LIBFLAGS)
pkglib_LTLIBRARIES += libtimer_base.la

libhfclock_la_SOURCES = hfclock.c
libhfclock_la_LIBADD = $(COMMON_LIBADD) libtimer_base.la
pkglib_LTLIBRARIES += libhfclock.la

if ENABLE_CRAY_POWER_SAMPLER
libcray_power_sampler_la_SOURCES = cray_power_sampler.c
libcray_power_sampler_la_CFLAGS = $(AM_CFLAGS)
libcray_power_sampler_la_LIBADD = $(COMMON_LIBADD) \
                             libtimer_base.la
pkglib_LTLIBRARIES += libcray_power_sampler.la
endif

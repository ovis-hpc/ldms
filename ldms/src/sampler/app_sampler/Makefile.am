pkglib_LTLIBRARIES = libapp_sampler.la

libapp_sampler_la_SOURCES = app_sampler.c
libapp_sampler_la_CFLAGS  = @OVIS_INCLUDE_ABS@
libapp_sampler_la_LIBADD  = -lsampler_base -lldms -lovis_util -lcoll \
			    @LDFLAGS_GETTIME@ ../../ldmsd/libldmsd_stream.la
libapp_sampler_la_LDFLAGS = @OVIS_LIB_ABS@

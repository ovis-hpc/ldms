pkglib_LTLIBRARIES = libapp_sampler.la
dist_man7_MANS =

COMMON_LIBS= -lsampler_base -lldms -lovis_util -lcoll \
	    @LDFLAGS_GETTIME@ ../../ldmsd/libldmsd_stream.la

libapp_sampler_la_SOURCES = app_sampler.c
libapp_sampler_la_CFLAGS  = @OVIS_INCLUDE_ABS@
libapp_sampler_la_LIBADD  =  $(COMMON_LIBS)
libapp_sampler_la_LDFLAGS = @OVIS_LIB_ABS@

pkglib_LTLIBRARIES += liblinux_proc_sampler.la

EXTRA_DIST=get_stat_field.c ldms-gen-syscall-map
bin_SCRIPTS = ldms-gen-syscall-map
dist_man7_MANS += Plugin_linux_proc_sampler.man
liblinux_proc_sampler_la_SOURCES = linux_proc_sampler.c
liblinux_proc_sampler_la_CFLAGS  = @OVIS_INCLUDE_ABS@
liblinux_proc_sampler_la_LIBADD  = $(COMMON_LIBS)
liblinux_proc_sampler_la_LDFLAGS = @OVIS_LIB_ABS@

check_PROGRAMS = test_fd_timing
test_fd_timing_SOURCES=test_fd_timing.c

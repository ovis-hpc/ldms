pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =


AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_PROCSTAT
libprocstat_la_SOURCES = procstat.c 
libprocstat_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libprocstat.la
dist_man7_MANS += Plugin_procstat.man
endif



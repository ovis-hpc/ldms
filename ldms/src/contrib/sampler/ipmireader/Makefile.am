SUBDIRS =
pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
bin_SCRIPTS=
dist_man7_MANS =
dist_man1_MANS =
EXTRA_DIST =

AM_LDFLAGS = @OVIS_LIB_ABS@
AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_IPMIREADER
libipmireader_la_SOURCES = ipmireader.c
libipmireader_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES += libipmireader.la
dist_man7_MANS += Plugin_ipmireader.man

libipmisensors_la_SOURCES = ipmisensors.c
libipmisensors_la_LIBADD = $(COMMON_LIBADD) 
pkglib_LTLIBRARIES += libipmisensors.la
dist_man7_MANS += Plugin_ipmisensors.man
endif
EXTRA_DIST += Plugin_ipmireader.man
EXTRA_DIST += Plugin_ipmisensors.man

pkglib_LTLIBRARIES =

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@
AM_LDFLAGS = @OVIS_LIB_ABS@
#COMMON_LIBADD = -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

COMMON_LIBADD = @LDFLAGS_GETTIME@ $(top_builddir)/lib/src/third/libovis_third.la

lib_LTLIBRARIES = liblshm.la

include_HEADERS = ldms_shm_event_set.h ldms_shm_obj.h ldms_shm_index.h
liblshm_la_SOURCES = ldms_shm_event_set.c ldms_shm_obj.c ldms_shm_index.c 
liblshm_la_SOURCES += $(include_HEADERS)
liblshm_la_LIBADD = $(COMMON_LIBADD) -lm -lrt -lpthread

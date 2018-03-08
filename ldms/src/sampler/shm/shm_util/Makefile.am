pkglib_LTLIBRARIES =

#CORE = ../../../core
#AM_CFLAGS = -I$(srcdir)/$(CORE) -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_CFLAGS = -I$(top_srcdir) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIB64DIR_FLAG@ @OVIS_LIB_LIBDIR_FLAG@
COMMON_LIBADD = @LDFLAGS_GETTIME@ -lovis_third

lib_LTLIBRARIES = liblshm.la

include_HEADERS = ldms_shm_event_set.h ldms_shm_obj.h ldms_shm_index.h
liblshm_la_SOURCES = ldms_shm_event_set.c ldms_shm_obj.c ldms_shm_index.c 
liblshm_la_SOURCES += $(include_HEADERS)
liblshm_la_LIBADD = $(COMMON_LIBADD) -lm -lrt -lpthread

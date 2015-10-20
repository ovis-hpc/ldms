sbin_PROGRAMS = me mectl

CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@
AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'

me_SOURCES = me.c
me_LDADD = $(CORE)/libme_core.la
me_LDFLAGS = $(AM_LDFLAGS) -ldl -lpthread -lzap -lcoll -lovis_util -locm \
	     @OCM_LIBDIR_FLAG@
me_CFLAGS = $(AM_CFLAGS) @OCM_INCDIR_FLAG@

mectl_SOURCES = mectl.c
mectl_LDADD = $(CORE)/libme.la
mectl_LDFLAGS = $(AM_LDFLAGS) -lovis_ctrl -lovis_util -lreadline

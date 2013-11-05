sbin_PROGRAMS = me mectl

CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE)

me_SOURCES = me.c
me_LDADD = $(CORE)/libme_core.la
me_LDFLAGS = -ldl -lpthread -lzap -lcoll -lovis_util -locm
me_CFLAGS = $(AM_CFLAGS)

mectl_SOURCES = mectl.c
mectl_LDADD = $(CORE)/libme.la
mectl_LDFLAGS = -lovis_ctrl -lovis_util -lreadline
mectl_CFLAGS = $(AM_CFLAGS)

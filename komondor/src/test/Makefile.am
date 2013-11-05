AM_CFLAGS = -I@OVIS_LIB_INCDIR@
AM_LDFLAGS = -L@OVIS_LIB_LIB64DIR@ -L@OVIS_LIB_LIBDIR@

sbin_PROGRAMS = fake_me

fake_me_SOURCES = fake_me.c
fake_me_LDADD = -lzap -lpthread

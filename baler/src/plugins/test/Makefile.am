AM_CFLAGS = -I@top_srcdir@/src/

bin_PROGRAMS = fake_me_recv
fake_me_recv_CFLAGS = -I@OVIS_LIB_INCDIR@
fake_me_recv_LDADD = -L@OVIS_LIB_LIBDIR@ -L@OVIS_LIB_LIB64DIR@ -lzap -lpthread \
		     -levent

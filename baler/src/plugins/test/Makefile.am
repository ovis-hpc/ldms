AM_CFLAGS = -I@top_srcdir@/src/

bin_PROGRAMS = fake_me_recv
fake_me_recv_CFLAGS = @OVIS_LIB_INCDIR_FLAG@ @EVENTINC@
fake_me_recv_LDADD = @OVIS_LIB_LIBDIR_FLAG@ -lzap -lpthread \
		     @EVENTLIBS@

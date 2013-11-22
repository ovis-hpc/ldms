sbin_PROGRAMS = me_gen_inputs

CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE) @OVIS_LIB_INCDIR_FLAG@
AM_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@

me_gen_inputs_SOURCES = me_gen_inputs.c
me_gen_inputs_LDADD = $(CORE)/libme.la -lzap -lcoll -lpthread

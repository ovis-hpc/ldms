sbin_PROGRAMS = me_gen_inputs

CORE = ../core
AM_CFLAGS = -I$(srcdir)/$(CORE)

me_gen_inputs_SOURCES = me_gen_inputs.c
me_gen_inputs_LDADD = $(CORE)/libme.la
me_gen_inputs_LDFLAGS = -lzap -lcoll -lpthread
me_gen_inputs_CFLAGS = $(AM_CFLAGS)

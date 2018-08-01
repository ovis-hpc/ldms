AM_CFLAGS = -I$(top_srcdir)/src
AM_LDFLAGS = -L$(top_srcdir)/src

if ENABLE_SOS
sbin_PROGRAMS = map_cmd

map_cmd_SOURCES = map_cmd.c
map_cmd_CFLAGS = $(AM_CFLAGS)
map_cmd_LDFLAGS = $(AM_LDFLAGS)
map_cmd_LDADD = ../../src/coll/libmap.la
endif

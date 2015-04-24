lib_LTLIBRARIES =
bin_PROGRAMS =

AM_CFLAGS = -I$(srcdir)/../

bcsv2bnumvec_SOURCES = bcsv2bnumvec.c
bcsv2bnumvec_LDADD = ../baler/libbaler.la
bin_PROGRAMS += bcsv2bnumvec

bnumvec_SOURCES = bnumvec.c
bnumvec_LDADD = ../baler/libbaler.la -lpthread -lm
bin_PROGRAMS += bnumvec

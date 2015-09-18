lib_LTLIBRARIES =
bin_PROGRAMS =

AM_CFLAGS = -I$(srcdir)/../

bcsv2bnumvec_SOURCES = bcsv2bnumvec.c bnum.h
bcsv2bnumvec_LDADD = ../baler/libbaler.la
bin_PROGRAMS += bcsv2bnumvec

bnumvec_SOURCES = bnumvec.c bnum.h
bnumvec_LDADD = ../baler/libbaler.la -lpthread -lm
bin_PROGRAMS += bnumvec

btkn_dump_SOURCES = btkn_dump.c
btkn_dump_LDADD = ../baler/libbaler.la
bin_PROGRAMS += btkn_dump

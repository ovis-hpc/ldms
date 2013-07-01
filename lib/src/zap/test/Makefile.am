ACLOCAL_AMFLAGS = -I m4
sbin_PROGRAMS = zap_test
AM_CFLAGS = -I$(srcdir)/../

zap_test_SOURCES = zap_test.c
zap_test_LDADD = $(builddir)/../libzap.la -lpthread -ldl

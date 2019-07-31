
AM_CPPFLAGS = -DPLUGINDIR='"$(pkglibdir)"'
AM_CFLAGS = -I..
libtada_la_SOURCES = tada.c
libtada_la_LIBADD = -ldl -lpthread -lcrypto
libtadaincludedir = $(includedir)/tada
libtadainclude_HEADERS = tada.h

lib_LTLIBRARIES = libtada.la

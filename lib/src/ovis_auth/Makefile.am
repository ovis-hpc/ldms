lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I..

libovis_auth_la_SOURCES = auth.c auth.h
libovis_authinclude_HEADERS =  auth.h
libovis_authincludedir = $(includedir)/ovis_auth
libovis_auth_conf = /etc/ld.so.conf.d/libovis_auth.conf
libovis_auth_la_LIBADD = @LDFLAGS_GETTIME@ @OPENSSL_LIBS@
lib_LTLIBRARIES += libovis_auth.la

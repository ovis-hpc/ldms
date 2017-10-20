EXTRA_DIST=ovis-auth.sh.in
lib_LTLIBRARIES =

AM_CFLAGS = -I$(srcdir)/../ -I..

libovis_auth_la_SOURCES = auth.c auth.h
libovis_authinclude_HEADERS =  auth.h
libovis_authincludedir = $(includedir)/ovis_auth
libovis_auth_conf = /etc/ld.so.conf.d/libovis_auth.conf
libovis_auth_la_LIBADD = @LDFLAGS_GETTIME@ @OPENSSL_LIBS@
lib_LTLIBRARIES += libovis_auth.la

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(pkglibdir)
	$(INSTALL) -m 644 ovis-auth.sh $(DESTDIR)$(pkglibdir)

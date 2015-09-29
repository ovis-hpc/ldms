INC = -I$(top_srcdir)/src @SOS_INCDIR_FLAG@
DEFS =

BUILT_SOURCES = bquery.c bquery.py
# SWIG_SOURCES
pkgpython_PYTHON = bhquery.py __init__.py
nodist_pkgpython_PYTHON = bquery.py
pkgpyexec_LTLIBRARIES = _bquery.la
nodist__bquery_la_SOURCES = bquery.c
_bquery_la_SOURCES = bquery.i
_bquery_la_CFLAGS = -g $(SWIG_PYTHON_CPPFLAGS) $(INC)
_bquery_la_LDFLAGS = -module -shared $(PYTHON_LDFLAGS) @SOS_LIBDIR_FLAG@
_bquery_la_LIBADD = $(top_builddir)/src/query/libbquery.la \
		    $(top_builddir)/src/baler/libbaler.la \
		    -lsos

pkgpyexecdir = $(pkgpythondir)

bquery.py: bquery.c

bquery.c: bquery.i $(top_srcdir)/src/query/bquery.h
	$(SWIG) $(SWIG_PYTHON_OPT) $(INC) -o $@ $<

install-data-hook:
	ln -sf $(pkgpythondir)/bhquery.py $(DESTDIR)$(bindir)/bhquery
	chmod 755 $(DESTDIR)$(pkgpythondir)/bhquery.py

uninstall-local:
	rm -f $(DESTDIR)$(bindir)/bhquery

clean-local:
	rm -f bquery.c bquery.py

# OLD

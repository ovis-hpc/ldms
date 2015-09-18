INC = -I$(top_srcdir)/src -I@SOS_INCDIR@
DEFS =

BUILT_SOURCES = bquery.c bquery.py
# SWIG_SOURCES =
pkgpython_PYTHON = bquery.py __init__.py
pkgpyexec_LTLIBRARIES = _bquery.la
_bquery_la_SOURCES = bquery.c bquery.i
_bquery_la_CFLAGS = -g $(SWIG_PYTHON_CPPFLAGS) $(INC)
_bquery_la_LDFLAGS = -module -shared $(PYTHON_LDFLAGS) @SOS_LIBDIR_FLAG@
_bquery_la_LIBADD = $(top_builddir)/src/query/libbquery.la \
		    $(top_builddir)/src/baler/libbaler.la \
		    -lsos

pkgpyexecdir = $(pkgpythondir)

bquery.py: bquery.c

bquery.c: bquery.i $(top_srcdir)/src/query/bquery.h
	$(SWIG) $(SWIG_PYTHON_OPT) $(INC) -o $@ $<

clean-local:
	rm -f bquery.c bquery.py

# OLD

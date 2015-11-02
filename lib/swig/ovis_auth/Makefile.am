# For Swig stuffs
if ENABLE_SWIG
BUILT_SOURCES = auth.c ovis_auth.py
SWIG_SOURCES = ovis_auth.i $(top_srcdir)/src/ovis_auth/auth.h
nodist_pkgpython_PYTHON = ovis_auth.py
pkgpyexec_LTLIBRARIES = _ovis_auth.la
_ovis_auth_la_SOURCES = $(SWIG_SOURCES)
nodist__ovis_auth_la_SOURCES = auth.c
_ovis_auth_la_CFLAGS = -g $(SWIG_PYTHON_CPPFLAGS) -I$(top_srcdir)/src
_ovis_auth_la_LDFLAGS = -module $(PYTHON_LDFLAGS)
_ovis_auth_la_LIBADD = $(top_builddir)/src/ovis_auth/libovis_auth.la

ovis_auth.py: auth.c

auth.c: $(SWIG_SOURCES)
	$(SWIG) $(SWIG_PYTHON_OPT) -I$(top_srcdir)/src -o $@ $<

clean-local:
	rm -f auth.c ovis_auth.py
endif

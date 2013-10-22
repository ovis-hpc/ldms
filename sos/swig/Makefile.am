# For Swig stuffs
BUILT_SOURCES = sos.c sos.py
SWIG_SOURCES = sos.i $(top_srcdir)/src/sos.h
pkgpython_PYTHON = sos.py __init__.py
pkgpyexec_LTLIBRARIES = _sos.la
_sos_la_SOURCES = sos.c $(SWIG_SOURCES)
_sos_la_CFLAGS = -g $(SWIG_PYTHON_CPPFLAGS) -I$(top_srcdir)/src
_sos_la_LDFLAGS = -module
_sos_la_LIBADD = \
	$(top_builddir)/src/libods.la \
	$(top_builddir)/src/libidx.la \
	$(top_builddir)/src/liboidx.la \
	$(top_builddir)/src/libsos.la

# Forcing this so that _sos.so will be in lib not lib64
#pkgpyexecdir = $(pkgpythondir)
# not a good idea; use correct pythonpath

sos.py: sos.c

sos.c: $(SWIG_SOURCES)
	$(SWIG) $(SWIG_PYTHON_OPT) -I$(top_srcdir)/src -o $@ $<

clean-local:
	rm -f sos.c sos.py

# $(PYTHON_SITE_PKG) is set by AC_PYTHON_DEVEL
# $(pkgpyexecdir) and $(pkgpythondir) is set by automake-python
# $(pythondir) and $(pyexecdir) is also set by automake-python

sos_pth = $(PYTHON_SITE_PKG)/sos.pth
# Disable this for non-root installation
#
#install-data-hook:
#	echo "$(pythondir)" > $(sos_pth)

#uninstall-local:
#	rm -f $(sos_pth)

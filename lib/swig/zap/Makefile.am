# For Swig stuffs
BUILT_SOURCES = zap.c zap.py
SWIG_SOURCES = zap.i $(top_srcdir)/src/zap/zap.h
pkgpython_PYTHON = zap.py __init__.py
pkgpyexec_LTLIBRARIES = _zap.la
_zap_la_SOURCES = zap.c $(SWIG_SOURCES)
_zap_la_CFLAGS = -g $(SWIG_PYTHON_CPPFLAGS) -I$(top_srcdir)/src
_zap_la_LDFLAGS = -module $(PYTHON_LDFLAGS)
_zap_la_LIBADD = $(top_builddir)/src/zap/libzap.la

zap.py: zap.c

zap.c: $(SWIG_SOURCES)
	$(SWIG) $(SWIG_PYTHON_OPT) -I$(top_srcdir)/src -o $@ $<

clean-local:
	rm -f zap.c zap.py

# $(PYTHON_SITE_PKG) is set by AC_PYTHON_DEVEL
# $(pkgpyexecdir) and $(pkgpythondir) is set by automake-python
# $(pythondir) and $(pyexecdir) is also set by automake-python

zap_pth = $(PYTHON_SITE_PKG)/zap.pth
# Disable this for non-root installation
#
#install-data-hook:
#	echo "$(pythondir)" > $(zap_pth)

#uninstall-local:
#	rm -f $(zap_pth)

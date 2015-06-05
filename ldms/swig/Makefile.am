BUILT_SOURCES = ldms.c ldms.py
SWIG_SOURCES = ldms.i $(top_srcdir)/src/core/ldms.h
pkgpython_PYTHON = ldms.py __init__.py
pkgpyexec_LTLIBRARIES = _ldms.la
_ldms_la_SOURCES = ldms.c $(SWIG_SOURCES)
_ldms_la_CFLAGS = -g $(SWIG_PYTHON_CPPFLAGS) -I$(top_srcdir)/src/core \
		 @OVIS_LIB_INCDIR_FLAG@
_ldms_la_LDFLAGS = @OVIS_LIB_LIBDIR_FLAG@  $(PYTHON_LDFLAGS) -module
_ldms_la_LIBADD = $(top_builddir)/src/core/libldms.la -lpthread

ldms.py : ldms.c

ldms.c : $(SWIG_SOURCES)
	$(SWIG) $(SWIG_PYTHON_OPT) -I$(top_srcdir)/src/core -o $@ $<

clean-local:
	rm -f ldms.c ldms.py

ldms_pth = $(PYTHON_SITE_PKG)/ldms.pth

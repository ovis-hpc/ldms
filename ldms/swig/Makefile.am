# swig generates code with type abuse which will fail to compile
# with -Wall,-Werror. So remove the -Werror so that the warnings will
# be but they don't kill the compile.
CFLAGS := $(filter-out -Werror, ${CFLAGS})

BUILT_SOURCES = ldms.c ldms.py
SWIG_SOURCES = ldms.i $(top_srcdir)/src/core/ldms.h $(top_srcdir)/src/core/ldms_core.h
pkgpython_PYTHON = __init__.py
nodist_pkgpython_PYTHON = ldms.py
pkgpyexec_LTLIBRARIES = _ldms.la
_ldms_la_SOURCES = $(SWIG_SOURCES)
nodist__ldms_la_SOURCES = ldms.c
_ldms_la_CFLAGS = -g $(SWIG_PYTHON_CPPFLAGS) -I$(top_srcdir)/src/core \
		 -I../src @OVIS_LIB_INCDIR_FLAG@
_ldms_la_LDFLAGS = -lovis_util \
		   @OVIS_LIB_LIBDIR_FLAG@ \
		   @OVIS_LIB_LIB64DIR_FLAG@ \
		   $(PYTHON_LDFLAGS) -module
_ldms_la_LIBADD = $(top_builddir)/src/core/libldms.la -lpthread

ldms.py : ldms.c

ldms.c : $(SWIG_SOURCES)
	$(SWIG) $(SWIG_PYTHON_OPT) -I$(top_srcdir)/src/core -o $@ $<

clean-local:
	rm -f ldms.c ldms.py

ldms_pth = $(PYTHON_SITE_PKG)/ldms.pth

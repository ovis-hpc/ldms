CFLAGS := $(filter-out -Werror, ${CFLAGS})

PY_INCLUDES = -I$(top_srcdir)/ldms/src/core \
	      -I$(top_builddir) \
	      -I$(top_srcdir)/lib/src

pkgpyexec_LTLIBRARIES = ldms.la

LDMS_PYX_C = ldms_pyx.c

ldms_la_SOURCES = ldms.pyx ldms.pxd
nodist_ldms_la_SOURCES = $(LDMS_PYX_C)
ldms_la_CFLAGS = -g -O0  $(PY_INCLUDES) $(PYTHON_CPPFLAGS)
ldms_la_LDFLAGS = $(PYTHON_LDFLAGS) -module -shared
ldms_la_LIBADD = $(top_builddir)/ldms/src/core/libldms.la

pkgpyexecdir = $(pythondir)/ovis_ldms

pkgpython_PYTHON = __init__.py

pkgpythondir = $(pythondir)/ovis_ldms

$(LDMS_PYX_C): ldms.pyx ldms.pxd
	echo PYTHON_LDFLAGS are "$(PYTHON_LDFLAGS)"
	cython --fast-fail --gdb --3str -I $(srcdir) $< -o $@

SUBDIRS = ldmsd cmds

clean-local:
	rm -f $(LDMS_PYX_C)

CFLAGS := $(filter-out -Werror, ${CFLAGS})

PY_INCLUDES = -I$(top_srcdir)/ldms/src/core \
		-I$(top_srcdir)/lib/src \
		-I$(top_builddir) \
		-I$(top_builddir)/src

pkgpythondir = ${pythondir}/ovis_ldms
pkgpython_PYTHON = __init__.py
pkgpyexecdir = $(pkgpythondir)

pkgpyexec_LTLIBRARIES = ldms.la


LDMS_PYX_C = ldms_pyx.c

nodist_ldms_la_SOURCES = $(LDMS_PYX_C)


ldms_la_SOURCES = ldms.pyx ldms.pxd
ldms_la_CFLAGS = -g -O0  $(PY_INCLUDES) $(PYTHON_CPPFLAGS)
ldms_la_LDFLAGS = $(PYTHON_LDFLAGS) -module -shared
ldms_la_LIBADD = $(top_builddir)/ldms/src/core/libldms.la

$(LDMS_PYX_C): ldms.pyx ldms.pxd
	echo PYTHON_LDFLAGS are "$(PYTHON_LDFLAGS)"
	$(CYTHON) -3 --directive language_level=3 --fast-fail --gdb -I $(srcdir) $< -o $@

SUBDIRS = ldmsd cmds

CLEANFILES = $(LDMS_PYX_C)

clean-local:
	-rm -rf cython_debug
	-rm -rf Y

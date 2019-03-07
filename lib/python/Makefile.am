CFLAGS := $(filter-out -Werror, ${CFLAGS})
PY_INCLUDES = -I$(srcdir)/../include \
	-I$(srcdir)/../src

pkgpyexec_LTLIBRARIES = Map.la

Map_la_SOURCES = Map.pyx Map.pxd
nodist_Map_la_SOURCES = Map.c
Map_la_CFLAGS = -g -O0 $(PY_INCLUDES) $(PYTHON_CPPFLAGS)
Map_la_LDFLAGS = $(PTHON_LDFLAGS) -module -shared
Map_la_LIBADD = $(top_builddir)/src/coll/libmap.la

pkgpyexecdir = $(pythondir)/ovis_lib

Map.c: Map.pyx Map.pxd
	echo PYTHON_LDFLAGS are "$(PYTHON_LDFLAGS)"
	cython --gdb $< -o $@

install-data-local: Map.la
	touch $(pythondir)/ovis_lib/__init__.py

clean-local:
	rm -rf Map.c

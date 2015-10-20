ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src
if ENABLE_ETC
SUBDIRS += etc
endif

PEDIGREE = $(DESTDIR)/$(bindir)/ocm-pedigree
include $(top_srcdir)/../automake/pedigree.am

EXTRA_DIST=autogen.sh COPYING

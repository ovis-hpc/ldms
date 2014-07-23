ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src scripts swig
if ENABLE_ETC
SUBDIRS += etc
endif

PEDIGREE = $(DESTDIR)/$(bindir)/baler-pedigree
include $(top_srcdir)/../automake/pedigree.am

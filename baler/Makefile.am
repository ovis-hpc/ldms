ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src scripts swig
if ENABLE_ETC
SUBDIRS += etc
endif

if ENABLE_DICT
SUBDIRS += dict
endif

PEDIGREE = $(DESTDIR)/$(bindir)/baler-pedigree
include $(top_srcdir)/../automake/pedigree.am

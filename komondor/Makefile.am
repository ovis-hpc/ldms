ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src python
if ENABLE_ETC
SUBDIRS += etc
endif

PEDIGREE = $(DESTDIR)/$(bindir)/komondor-pedigree
include $(top_srcdir)/../automake/pedigree.am

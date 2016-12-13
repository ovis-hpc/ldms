if ENABLE_BALER

EXTRA_DIST = baler.cfg hosts.txt SHA.txt TAG.txt

ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

if ENABLE_SWIG
SUBDIRS += swig
SUBDIRS += python
endif

if ENABLE_ETC
SUBDIRS += etc
endif

if ENABLE_DICT
SUBDIRS += dict
endif

if ENABLE_DOC
SUBDIRS += docs
endif

endif

PEDIGREE = $(DESTDIR)/$(bindir)/baler-pedigree
include $(top_srcdir)/pedigree.am

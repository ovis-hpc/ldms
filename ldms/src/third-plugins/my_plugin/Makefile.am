ACLOCAL_AMFLAGS = -I m4
#
# This top-level make file works when everything is
# built in the required order from the top.
# Manually configuring parts individually and expecting
# the whole to build is obviously difficult.
# Use the top configure if you expect to use the top make.
#
EXTRA_DIST= m4/my_plugin_top.m4

docdir = $(datadir)/doc/@PACKAGE@
# dist_doc_DATA = README.txt

SUBDIRS = scripts src test

BASE = $(PACKAGE_NAME)-$(PACKAGE_VERSION)
BASE_TARBALL = $(BASE).tar.gz
IPV=-$(PACKAGE_VERSION)

.PHONY: doxygen

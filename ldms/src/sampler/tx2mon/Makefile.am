include $(top_srcdir)/ldms/rules.mk


#  Copyright [2020] Hewlett Packard Enterprise Development LP
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License as published
# by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to:
#
#   Free Software Foundation, Inc.
#   51 Franklin Street, Fifth Floor
#   Boston, MA 02110-1301, USA.

AM_CPPFLAGS = @OVIS_INCLUDE_ABS@

AM_LDFLAGS = @OVIS_LIB_ABS@

COMMON_LIBADD = -lsampler_base -lldms @LDFLAGS_GETTIME@ -lovis_util -lcoll

if ENABLE_TX2MON
libtx2mon_la_SOURCES = tx2mon_cli.c tx2mon.c tx2mon.h
libtx2mon_la_LIBADD = $(COMMON_LIBADD)
pkglib_LTLIBRARIES = libtx2mon.la
dist_man7_MANS = ldms-sampler_tx2mon.man
endif

CLEANFILES = $(dist_man7_MANS)

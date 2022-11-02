# Copyright (c) 2022 Intel Corporation
# Copyright (c) 2011-2018 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2011-2018 Open Grid Computing, Inc. All rights reserved.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the BSD-type
# license below:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#      Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#      Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditions and the following
#      disclaimer in the documentation and/or other materials provided
#      with the distribution.
#
#      Neither the name of Sandia nor the names of any contributors may
#      be used to endorse or promote products derived from this software
#      without specific prior written permission.
#
#      Neither the name of Open Grid Computing nor the names of any
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
#      Modified source versions must be plainly marked as such, and
#      must not be misrepresented as being the original software.
#
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


pkglib_LTLIBRARIES =
lib_LTLIBRARIES =
dist_man7_MANS =
dist_man1_MANS =


CFLAGS0 = -Dsanitize_address=true
CFLAGS1 = -D_FORTIFY_SOURCE=2 -O2 -D_GLIBCXX_ASSERTIONS -fasynchronous-unwind-tables -fexceptions -fpie -fPIC
CFLAGS2 = -fstack-protector-strong -Werror=format-security -Wall
LDFLAGS0 = -Wl,-z,defs -Wl,-z,now -Wl,-z,relro

SDLCFLAGS = $(CFLAGS0) $(CFLAGS1) $(CFLAGS2) $(CFLAGSIMULATION)
SDLLDFLAGS = $(LDFLAGS0)


AM_CPPFLAGS = @OVIS_INCLUDE_ABS@ $(SDLCFLAGS)
AM_LDFLAGS = @OVIS_LIB_ABS@ $(SDLLDFLAGS)
COMMON_LIBADD = -lsampler_base -lldms -lovis_util -lcoll -lovis_log\
		@LDFLAGS_GETTIME@
GMGLIBS = -L. -lgmg -L/usr/lib/x86_64-linux-gnu/ -lze_loader

if ENABLE_GPU_METRICS


noinst_LIBRARIES = libgmg.a
libgmg_a_SOURCES = gmg_common_util.c gmg_log.c gather_gpu_metrics_from_one_api.c gmg_ldms_util.c

libgpumetrics_la_SOURCES = gpu_metrics_ldms_sampler.c
libgpumetrics_la_LIBADD = $(COMMON_LIBADD) $(GMGLIBS)
pkglib_LTLIBRARIES += libgpumetrics.la

bin_PROGRAMS = bin/gpu_metrics_gatherer
bin_gpu_metrics_gatherer_SOURCES = gather_gpu_metrics.cpp
bin_gpu_metrics_gatherer_LDADD = $(GMGLIBS)


endif

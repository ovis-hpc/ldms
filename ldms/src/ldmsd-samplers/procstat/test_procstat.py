#!/usr/bin/env python

# Copyright (c) 2019 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
#
# Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
# license for use of this work by or on behalf of the U.S. Government.
# Export of this program may require a license from the United States
# Government.
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

import os
import re
import pdb
import sys
import time
import socket
import shutil
import logging
import unittest

from collections import namedtuple

from ldmsd.chroot import D, LDMSData, LDMSChrootTest, try_sudo, xvars, \
                         Src, SrcData

HOSTNAME = socket.gethostname()
LDMSD_PREFIX = HOSTNAME + ":" + str(LDMSChrootTest.PORT)
INST_NAME = LDMSD_PREFIX + "/test"
SCHEMA_NAME = "procstat"
MAXCPU = 16
DIR = "test_procstat" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src = [ # 1 Src() per source file
    Src("/proc/stat",
"""\
cpu  1488749 5304 304096 209077917 57965 0 3437 0 0 0
cpu0 190512 660 35611 26137004 409 0 474 0 0 0
cpu1 186922 640 35832 26146732 345 0 124 0 0 0
cpu2 183739 666 35989 26149061 318 0 55 0 0 0
cpu3 180603 703 33159 26156168 226 0 29 0 0 0
cpu4 193394 691 33372 26119219 24485 0 337 0 0 0
cpu5 182383 650 47396 26097434 30380 0 9 0 0 0
cpu6 189129 656 43032 26132068 1174 0 78 0 0 0
cpu7 182065 637 39702 26140227 624 0 2328 0 0 0
intr 60413735 9 0 0 0 0 0 0 0 1 4 0 0 0 0 0 0 31 0 0 0 0 0 0 2848373 0 405131 0 0 0 0 0 0 0 789951 0 2583703 4356010 13 2008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
ctxt 272555626
btime 1554734246
processes 218832
procs_running 2
procs_blocked 0
softirq 53689612 407995 17328446 50958 2645496 775408 0 986715 18030798 0 13463796
"""
    ),
    # More SrcData() record for 2nd ldms update
    Src("/proc/stat",
"""\
cpu  1488749 5304 304096 209077917 57965 0 3437 8 8 8
cpu0 190512 660 35611 26137004 409 0 474 1 1 1
cpu1 186922 640 35832 26146732 345 0 124 1 1 1
cpu2 183739 666 35989 26149061 318 0 55 1 1 1
cpu3 180603 703 33159 26156168 226 0 29 1 1 1
cpu4 193394 691 33372 26119219 24485 0 337 1 1 1
cpu5 182383 650 47396 26097434 30380 0 9 1 1 1
cpu6 189129 656 43032 26132068 1174 0 78 1 1 1
cpu7 182065 637 39702 26140227 624 0 2328 1 1 1
intr 60413735 9 0 0 0 0 0 0 0 1 4 0 0 0 0 0 0 31 0 0 0 0 0 0 2848373 0 405131 0 0 0 0 0 0 0 789951 0 2583703 4356010 13 2008 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
ctxt 272555626
btime 1554734246
processes 318832
procs_running 2
procs_blocked 1
softirq 53689612 407995 17328446 50958 2645496 775408 0 986715 18030798 0 13463796
"""
    ),
]

def src2ldmsdata(src):
    metrics = dict()
    cpu_cols = ["user", "nice", "sys", "idle", "iowait",
                "irq", "softirq", "steal", "guest", "guest_nice"]
    per_core = { k: list() for k in cpu_cols }
    RE = re.compile("\\S+")
    for l in src.content.splitlines():
        tkns = RE.findall(l)
        name = tkns.pop(0)
        if name == "cpu":
            for k, v in zip(cpu_cols, tkns):
                metrics[k] = long(v)
        elif name.startswith("cpu"):
            for k, v in zip(cpu_cols, tkns):
                per_core[k].append(long(v))
        elif name == "intr":
            metrics["hwintr_count"] = long(tkns[0])
        elif name == "ctxt":
            metrics["context_switches"] = long(tkns[0])
        elif name in ["processes", "procs_running", "procs_blocked"]:
            metrics[name] = long(tkns[0])
        elif name == "softirq":
            metrics["softirq_count"] = long(tkns[0])
    # cpu_enabled
    metrics["cpu_enabled"] = 1
    cpus = len(per_core["user"])
    metrics["cores_up"] = cpus
    per_core["cpu_enabled"] = [1]*cpus
    # extend per_core metrics, filling 0's and assign to metrics
    for k, v in per_core.iteritems():
        name = "per_core_" + k
        metrics[name] = tuple(v + [0]*(MAXCPU-cpus))
    return LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": metrics,
            })


class ProcstatTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"
    _MAXCPU = MAXCPU

    @classmethod
    def getPluginName(cls):
        return "procstat"

    @classmethod
    def getPluginParams(cls):
        return { "maxcpu": cls._MAXCPU }

    @classmethod
    def getSrcData(cls, tidx):
        s = src[tidx]
        return SrcData(s, src2ldmsdata(s))


if __name__ == "__main__":
    try_sudo()
    pystart = os.getenv("PYTHONSTARTUP")
    if pystart:
        execfile(pystart)
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = DIR + "/test.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

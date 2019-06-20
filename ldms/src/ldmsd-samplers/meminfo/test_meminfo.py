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

from ldmsd.chroot import D, LDMSData, LDMSChrootTest, try_sudo, xvars, Src, \
                         SrcData

HOSTNAME = socket.gethostname()
LDMSD_PREFIX = HOSTNAME + ":" + str(LDMSChrootTest.PORT)
DIR = "test_meminfo" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src_data = [
SrcData(Src("/proc/meminfo",
"""\
MemTotal:       32367228 kB
MemFree:        20445628 kB
MemAvailable:   27341052 kB
Buffers:         1462996 kB
Cached:          5849868 kB
SwapCached:            0 kB
Active:          7348560 kB
Inactive:        3545304 kB
Active(anon):    3582472 kB
Inactive(anon):   692004 kB
Active(file):    3766088 kB
Inactive(file):  2853300 kB
Unevictable:          64 kB
Mlocked:              64 kB
SwapTotal:       2097148 kB
SwapFree:        2097148 kB
Dirty:               172 kB
Writeback:             0 kB
AnonPages:       3581244 kB
Mapped:           993828 kB
Shmem:            693484 kB
Slab:             813140 kB
SReclaimable:     738492 kB
SUnreclaim:        74648 kB
KernelStack:       17328 kB
PageTables:        65920 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    18280760 kB
Committed_AS:   11090120 kB
VmallocTotal:   34359738367 kB
VmallocUsed:           0 kB
VmallocChunk:          0 kB
HardwareCorrupted:     0 kB
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
CmaTotal:              0 kB
CmaFree:               0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
DirectMap4k:      323972 kB
DirectMap2M:    32649216 kB
"""),
LDMSData({
    "instance_name": LDMSD_PREFIX + "/test",
    "schema_name": "meminfo",
    "metrics": {
        "MemTotal":       32367228,
        "MemFree":        20445628,
        "MemAvailable":   27341052,
        "Buffers":         1462996,
        "Cached":          5849868,
        "SwapCached":            0,
        "Active":          7348560,
        "Inactive":        3545304,
        "Active(anon)":    3582472,
        "Inactive(anon)":   692004,
        "Active(file)":    3766088,
        "Inactive(file)":  2853300,
        "Unevictable":          64,
        "Mlocked":              64,
        "SwapTotal":       2097148,
        "SwapFree":        2097148,
        "Dirty":               172,
        "Writeback":             0,
        "AnonPages":       3581244,
        "Mapped":           993828,
        "Shmem":            693484,
        "Slab":             813140,
        "SReclaimable":     738492,
        "SUnreclaim":        74648,
        "KernelStack":       17328,
        "PageTables":        65920,
        "NFS_Unstable":          0,
        "Bounce":                0,
        "WritebackTmp":          0,
        "CommitLimit":    18280760,
        "Committed_AS":   11090120,
        "VmallocTotal":   34359738367,
        "VmallocUsed":           0,
        "VmallocChunk":          0,
        "HardwareCorrupted":     0,
        "AnonHugePages":         0,
        "ShmemHugePages":        0,
        "ShmemPmdMapped":        0,
        "CmaTotal":              0,
        "CmaFree":               0,
        "HugePages_Total":       0,
        "HugePages_Free":        0,
        "HugePages_Rsvd":        0,
        "HugePages_Surp":        0,
        "Hugepagesize":       2048,
        "DirectMap4k":      323972,
        "DirectMap2M":    32649216,
    }
})),
SrcData(Src("/proc/meminfo",
"""\
MemTotal:       32367228 kB
MemFree:        20438268 kB
MemAvailable:   27333924 kB
Buffers:         1463220 kB
Cached:          5849772 kB
SwapCached:            0 kB
Active:          7358804 kB
Inactive:        3542260 kB
Active(anon):    3589516 kB
Inactive(anon):   691928 kB
Active(file):    3769288 kB
Inactive(file):  2850332 kB
Unevictable:          64 kB
Mlocked:              64 kB
SwapTotal:       2097148 kB
SwapFree:        2097148 kB
Dirty:               232 kB
Writeback:             0 kB
AnonPages:       3588240 kB
Mapped:           994132 kB
Shmem:            693380 kB
Slab:             812888 kB
SReclaimable:     738492 kB
SUnreclaim:        74396 kB
KernelStack:       17168 kB
PageTables:        65944 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    18280760 kB
Committed_AS:   11120268 kB
VmallocTotal:   34359738367 kB
VmallocUsed:           0 kB
VmallocChunk:          0 kB
HardwareCorrupted:     0 kB
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
CmaTotal:              0 kB
CmaFree:               0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
DirectMap4k:      323972 kB
DirectMap2M:    32649216 kB
"""),
LDMSData({
    "instance_name": LDMSD_PREFIX + "/test",
    "schema_name": "meminfo",
    "metrics": {
        "MemTotal":       32367228,
        "MemFree":        20438268,
        "MemAvailable":   27333924,
        "Buffers":         1463220,
        "Cached":          5849772,
        "SwapCached":            0,
        "Active":          7358804,
        "Inactive":        3542260,
        "Active(anon)":    3589516,
        "Inactive(anon)":   691928,
        "Active(file)":    3769288,
        "Inactive(file)":  2850332,
        "Unevictable":          64,
        "Mlocked":              64,
        "SwapTotal":       2097148,
        "SwapFree":        2097148,
        "Dirty":               232,
        "Writeback":             0,
        "AnonPages":       3588240,
        "Mapped":           994132,
        "Shmem":            693380,
        "Slab":             812888,
        "SReclaimable":     738492,
        "SUnreclaim":        74396,
        "KernelStack":       17168,
        "PageTables":        65944,
        "NFS_Unstable":          0,
        "Bounce":                0,
        "WritebackTmp":          0,
        "CommitLimit":    18280760,
        "Committed_AS":   11120268,
        "VmallocTotal":   34359738367,
        "VmallocUsed":           0,
        "VmallocChunk":          0,
        "HardwareCorrupted":     0,
        "AnonHugePages":         0,
        "ShmemHugePages":        0,
        "ShmemPmdMapped":        0,
        "CmaTotal":              0,
        "CmaFree":               0,
        "HugePages_Total":       0,
        "HugePages_Free":        0,
        "HugePages_Rsvd":        0,
        "HugePages_Surp":        0,
        "Hugepagesize":       2048,
        "DirectMap4k":      323972,
        "DirectMap2M":    32649216,
    }
}))
]

class MeminfoTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def getPluginName(cls):
        return "meminfo"

    @classmethod
    def getSrcData(cls, tidx):
        return src_data[tidx]


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
            filename = DIR + "/test_meminfo.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

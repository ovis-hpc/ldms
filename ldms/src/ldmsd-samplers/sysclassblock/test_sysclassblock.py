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
SCHEMA_NAME = "sysclassblock"
DIR = "test_sysclassblock" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

SDA_DIR = "/sys/devices/pci0000:00/0000:00:17.0/ata1/host0/" \
          "target0:0:0/0:0:0:0/block/sda"

src_data = [
    # 1st SrcData() record for initialization + 1st ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src(SDA_DIR + "/queue/hw_sector_size", "512"),
            Src(SDA_DIR + "/stat",
                "257739    33065 15486109    84912   549876   "
                "338338 1167148067  4849576        0   188316  4935124"),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "reads_comp": 257739,
                    "reads_merg": 33065,
                    "sect_read": 15486109,
                    "time_read": 84912,
                    "writes_comp": 549876,
                    "writes_merg": 338338,
                    "sect_written": 1167148067,
                    "time_write": 4849576,
                    "ios_in_progress": 0,
                    "time_ios": 188316,
                    "weighted_time": 4935124,

                    "disk.byte_read": 15486109*512,
                    "disk.byte_written": 1167148067*512,
                }
            }),
        ]
    ),
    # More SrcData() record for 2nd ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src(SDA_DIR + "/queue/hw_sector_size", "512"),
            Src(SDA_DIR + "/stat",
                "281376    33514 15936555    89276   582810   "
                "371695 1168466519  4868180        0   198084  4958076"),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "reads_comp": 281376,
                    "reads_merg": 33514,
                    "sect_read": 15936555,
                    "time_read": 89276,
                    "writes_comp": 582810,
                    "writes_merg": 371695,
                    "sect_written": 1168466519,
                    "time_write": 4868180,
                    "ios_in_progress": 0,
                    "time_ios": 198084,
                    "weighted_time": 4958076,

                    "disk.byte_read": 15936555*512,
                    "disk.byte_written": 1168466519*512,
                }
            }),
        ]
    ),
]

def ln_s(name, src):
    # NOTE: `os.symlink()` does not allow symlink to non-existing src, which
    #       we need for chroot dir prep.
    cmd = "ln -s %s %s" % (src, name)
    os.system(cmd)

class SCBTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def initSources(cls):
        os.makedirs(cls.CHROOT_DIR + "/sys/class/block")
        ln_s(cls.CHROOT_DIR + "/sys/class/block/sda",
                   "../../devices/pci0000:00/0000:00:17.0/"
                   "ata1/host0/target0:0:0/0:0:0:0/block/sda")
        super(SCBTest, cls).initSources()

    @classmethod
    def cleanUpSources(cls):
        super(SCBTest, cls).cleanUpSources()
        os.remove(cls.CHROOT_DIR + "/sys/class/block/sda")
        os.removedirs(cls.CHROOT_DIR + "/sys/class/block")

    @classmethod
    def getPluginName(cls):
        return "sysclassblock"

    @classmethod
    def getPluginParams(cls):
        return { "dev": "sda" }

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

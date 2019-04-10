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
SCHEMA_NAME = "procnetdev"
DIR = "test_procnetdev" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src_data = [
    # 1st SrcData() record for initialization + 1st ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src("/proc/net/dev",
                """\
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
wlp17s0:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0
    lo: 21777730   61486    0    0    0     0          0         0 21777730   61486    0    0    0     0       0          0
  eno1: 3716612055 3094869    0    0    0     0          0      9223 196675177 1664275    0    0    0     0       0          0
                """),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "rx_bytes"      : 21777730,
                    "rx_packets"    : 61486,
                    "rx_errs"       : 0,
                    "rx_drop"       : 0,
                    "rx_fifo"       : 0,
                    "rx_frame"      : 0,
                    "rx_compressed" : 0,
                    "rx_multicast"  : 0,
                    "tx_bytes"      : 21777730,
                    "tx_packets"    : 61486,
                    "tx_errs"       : 0,
                    "tx_drop"       : 0,
                    "tx_fifo"       : 0,
                    "tx_colls"      : 0,
                    "tx_carrier"    : 0,
                    "tx_compressed" : 0,
                }
            }),
        ]
    ),
    # More SrcData() record for 2nd ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src("/proc/net/dev",
                """\
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
wlp17s0:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0
    lo: 31777730   71486    0    0    0     0          0         0 41777730   81486    0    0    0     0       0          0
  eno1: 3716612055 3094869    0    0    0     0          0      9223 196675177 1664275    0    0    0     0       0          0
                """),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "rx_bytes"      : 31777730,
                    "rx_packets"    : 71486,
                    "rx_errs"       : 0,
                    "rx_drop"       : 0,
                    "rx_fifo"       : 0,
                    "rx_frame"      : 0,
                    "rx_compressed" : 0,
                    "rx_multicast"  : 0,
                    "tx_bytes"      : 41777730,
                    "tx_packets"    : 81486,
                    "tx_errs"       : 0,
                    "tx_drop"       : 0,
                    "tx_fifo"       : 0,
                    "tx_colls"      : 0,
                    "tx_carrier"    : 0,
                    "tx_compressed" : 0,
                }
            }),
        ]
    ),
]

class ProcnetdevTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def getPluginName(cls):
        return "procnetdev"

    @classmethod
    def getPluginParams(cls):
        return { "dev": "lo" }

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

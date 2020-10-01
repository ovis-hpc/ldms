#!/usr/bin/env python3

# Copyright (c) 2018 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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

# This file contains test cases for ldmsd configuration files

from builtins import zip
from builtins import range
from builtins import object
import logging
import unittest
import threading
import time
import re
import json
import subprocess
import os
import fcntl
import errno
import pty
from io import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class Debug(object): pass

DEBUG = Debug()

ldms.init(256*1024*1024)

class TestLDMSDTsampler(unittest.TestCase):
    """Test cases for tsampler"""
    XPRT = "sock"
    SMP_PORT = "10200"
    SMP_LOG = "smp.log"

    # LDMSD instances
    smp = None

    @classmethod
    def setUpClass(cls):
        # 1 sampler, 1 aggregator
        log.info("Setting up " + cls.__name__)
        try:
            # samplers (producers)
            smp_cfg = """
load name=hfclock
config name=hfclock producer=smp instance=smp/hfclock schema=hfclock hfinterval=100000 hfcount=10
start name=hfclock interval=1000000 offset=0
            """
            log.debug("smp_cfg: %s" % smp_cfg)
            cls.smp = LDMSD(port = cls.SMP_PORT, xprt = cls.XPRT,
                            cfg = smp_cfg, logfile = cls.SMP_LOG)
            log.info("starting sampler")
            cls.smp.run()
            time.sleep(1)
            # aggregator will be configured later in the test cases

        except:
            del cls.smp
            raise
        log.info(cls.__name__ + " set up done")

    @classmethod
    def tearDownClass(cls):
        del cls

    def setUp(self):
        log.debug("---- %s ----" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def test_tsampler(self):
        time.sleep(1) # wait for sampler to populate the set
        #x = ldms.LDMS_xprt_new(self.XPRT)
        x = ldms.Xprt(name=self.XPRT)
        x.connect(host='localhost', port=self.SMP_PORT)
        s = x.lookup("smp/hfclock")
        s.update()
        clk = s["clock"]
        DEBUG.clk = clk
        tmp = s["clock_timeval"]
        clk_tv = [ tmp[2*i] + tmp[2*i+1]*1e-6 for i in range(0, len(tmp)//2) ]
        DEBUG.clk_tv = clk_tv
        for (a, b) in zip(clk, clk_tv):
            self.assertGreater(a, 0)
            self.assertLess(abs(a-b), 0.001)


if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_tsampler.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

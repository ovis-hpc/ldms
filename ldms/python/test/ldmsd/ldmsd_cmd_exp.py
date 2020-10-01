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
import socket
from io import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD, LDMSD_Controller
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class TestLDMSDCmdExp(unittest.TestCase):
    """Test cases for command expansion in ldmsd config file and request"""
    XPRT = "sock"
    SMP_PORT = "10200"
    SMP_LOG = "smp.log"

    # LDMSD instances
    smp = None

    @classmethod
    def setUpClass(cls):
        # 1 sampler
        log.info("Setting up " + cls.__name__)
        try:
            # samplers (producers)
            smp_cfg = """
                env H=$(hostname)
                load name=meminfo
                config name=meminfo producer=${H} \
                       instance=${H}/$(whoami)/meminfo schema=meminfo
                start name=meminfo interval=1000000 offset=0
            """
            log.debug("smp_cfg: %s" % smp_cfg)
            cls.smp = LDMSD(port = cls.SMP_PORT, xprt = cls.XPRT,
                            cfg = smp_cfg,
                            logfile = cls.SMP_LOG)
            log.info("starting sampler")
            cls.smp.run()
            time.sleep(1)

        except:
            del cls.smp
            raise
        log.info(cls.__name__ + " set up done")

    @classmethod
    def tearDownClass(cls):
        pass
        #del cls.smp

    def setUp(self):
        log.debug("---- %s ----" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def test_00_verify_cfg(self):
        """Verify sampler config, cmd-expand only env command"""
        host = socket.gethostname()
        xprt = ldms.Xprt(self.XPRT)
        xprt.connect("localhost", "10200")
        dir_ = []
        dir_resp = xprt.dir()
        for d in dir_resp:
            dir_.append(d.name)
        self.assertEqual(dir_, [host + "/$(whoami)/meminfo"])

    def test_01_req_noexp(self):
        """Request over xprt shall not be command-expanded"""
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT)
        ctrl.run()
        ctrl.read_pty()
        ctrl.write_pty("env X=$(hostname)\n")
        ctrl.write_pty("load name=vmstat\n")
        ctrl.write_pty("config name=vmstat producer=${X} \
                               instance=${X}/vmstat\
                               schema=vmstat\n")
        ctrl.write_pty("start name=vmstat interval=1000000 offset=0\n")
        time.sleep(10)

        host = socket.gethostname()
        xprt = ldms.Xprt(self.XPRT)
        xprt.connect("localhost", "10200")
        dir_resp = xprt.dir()
        dir_ = []
        for d in dir_resp:
            dir_.append(d.name)
        dir_.sort()
        expected = [ host + "/$(whoami)/meminfo", "$(hostname)/vmstat" ]
        expected.sort()
        self.assertEqual(dir_, expected)


if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_cmd_exp.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

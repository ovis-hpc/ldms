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

# This file contains test cases for various ldmsd controller commands

from builtins import str
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
from ldmsd.ldmsd_util import LDMSD, LDMSD_Controller
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class Debug(object): pass

DEBUG = Debug()

class TestLDMSDController(unittest.TestCase):
    LDMSD_UID = os.geteuid()
    LDMSD_GID = os.getegid()
    AUTH = "naive"
    LDMSD_AUTH_OPT = {"uid": LDMSD_UID, "gid": LDMSD_GID}
    XPRT = "sock"
    SMP_PORT = "10002"
    SMP_LOG = None
    AGG_PORT = "10000"
    AGG_LOG = None

    # LDMSD instances
    smp = None
    agg = None

    # ldmsd_controller
    ctrl = None

    @classmethod
    def setUpClass(cls):
        # 1 sampler, 1 aggregator
        log.info("Setting up " + cls.__name__)
        try:
            # samplers (producers)
            smp_cfg = """
                load name=meminfo
                config name=meminfo producer=smp \
                       instance=smp/meminfo schema=meminfo
                start name=meminfo interval=1000000 offset=0
            """
            log.debug("smp_cfg: %s" % smp_cfg)
            cls.smp = LDMSD(port = cls.SMP_PORT, xprt = cls.XPRT,
                          auth = cls.AUTH, auth_opt = cls.LDMSD_AUTH_OPT,
                          cfg = smp_cfg,
                          logfile = cls.SMP_LOG)
            log.info("starting sampler")
            cls.smp.run()

            # aggregator
            cls.agg = LDMSD(port = cls.AGG_PORT, xprt = cls.XPRT,
                            auth = cls.AUTH, auth_opt = cls.LDMSD_AUTH_OPT,
                            logfile = cls.AGG_LOG)
            log.info("starting aggregator")
            cls.agg.run()
            time.sleep(1)

            # Now, setup the ldmsd_controller subprocess
            cls.ctrl = LDMSD_Controller(port = cls.AGG_PORT, xprt = cls.XPRT,
                                        auth = cls.AUTH,
                                        auth_opt = cls.LDMSD_AUTH_OPT)
            cls.ctrl.run()
            cls.ctrl.read_pty() # discard the welcome message and the prompt
        except:
            del cls.agg
            del cls.smp
            del cls.ctrl
            raise
        log.info(cls.__name__ + " set up done")

    @classmethod
    def tearDownClass(cls):
        del cls.smp
        del cls.agg
        del cls.ctrl

    def setUp(self):
        log.debug("Testing: %s" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def _ctrl(self, cmd):
        cmd = cmd.strip()
        self.ctrl.write_pty(cmd + "\n")
        resp = self.ctrl.read_pty()
        lines = resp.splitlines()
        log.debug("_ctrl resp:" + str(lines))
        n = len(lines)
        # prompt
        print(lines)
        assert(lines[-1] == ("sock:localhost:%s> " % self.AGG_PORT))
        return lines[0:n-1]

    def test_0_prdcr_bogus(self):
        resp = self._ctrl("prdcr_start name=bogus")
        self.assertEqual(len(resp), 1)
        self.assertTrue(re.match(".* not exist.*", resp[0]))

    def test_0_bogus_command(self):
        resp = self._ctrl("bogus")
        self.assertEqual(len(resp), 1)
        self.assertTrue(re.match(".* Unknown syntax:.*", resp[0]))
        self.assertTrue(self.agg.is_running())

    def test_0_bogus_param(self):
        resp = self._ctrl("load name=blablabla")
        self.assertEqual(len(resp), 1)
        self.assertTrue(re.match(".*Failed to load the plugin.*", resp[0]))
        self.assertTrue(self.agg.is_running())

    def test_1_prdcr_updtr(self):
        cmds = """prdcr_add name=prdcr host=localhost xprt=%(xprt)s port=%(port)s type=active interval=1000000
            prdcr_start name=prdcr
            updtr_add name=updtr interval=1000000 offset=500000
            updtr_prdcr_add name=updtr regex=prdcr
            updtr_start name=updtr""" % {
                "xprt": self.XPRT,
                "port": self.SMP_PORT,
            }
        DEBUG.cmds = cmds
        for cmd in cmds.splitlines():
            resp = self._ctrl(cmd)
            errmsg = "cmd: %s\nresp: %s\n" % (cmd, resp)
            self.assertEqual(resp, [], msg = errmsg)
        time.sleep(2)
        # Now, check updtr and prdcr statuses
        resp = self._ctrl("prdcr_status")
        self.assertTrue(re.match(".*\sCONNECTED\s*.*", resp[2]))
        self.assertTrue(re.match(".*\smeminfo\s.*\sREADY\s*.*", resp[3]))
        resp = self._ctrl("updtr_status")
        self.assertTrue(re.match("updtr\s+.*\sRUNNING\s*.*", resp[2]))
        self.assertTrue(re.match("\s+prdcr\s+.*\sCONNECTED\s*.*", resp[3]))


if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_ctrl.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

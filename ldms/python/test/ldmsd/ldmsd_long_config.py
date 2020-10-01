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
import subprocess
import os
import pty
from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD, LDMSD_Controller
from time import sleep
import tempfile

log = logging.getLogger(__name__)

def on_timeout(fn, timeout = 1, step = 0.01, **kwarg):
    """Wrap a true/false function under a timeout
    The function is_fn() will be called until it returned True or the timeout is reached.

    @param fn        a function returning a boolean
    @param timeout      timeout in second
    @param step         interval in second to call is_fn() again
    """
    dur = 0
    while not fn(**kwarg):
        if dur >= timeout:
            return False
        sleep(step)
        dur += step
        fn(**kwarg)
    return True

class TestLDMSDLongConfig(unittest.TestCase):
    """Test cases focusing on long configuration line"""
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = "smp.log"
    LEN = 65536

    # LDMSD instances
    smp = None

    @classmethod
    def setUpClass(cls):
        pass

    @classmethod
    def tearDownClass(cls):
        pass

    def setUp(self):
        log.debug("---- %s ----" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")
        if self.smp:
            del self.smp

    def getMaxMsgSz(self, mode = ["configFile", "ldmsctl", "ldmsd_controller"]):
        if mode == "configFile":
            return 65536 # ldmsd config file max rec len is 8192.
        else:
            return 1100000 # ldms_xprt_msg_max() for socket is 1048536. This could be varied by system to system.

    def getGreetingCfgCmd(self, _len):
        return """greeting name=%s"""% ("a" * _len)

    def is_logfile_ready(self, filename):
        """Check if the ldmsd log file is created.
        """
        if os.path.isfile(filename):
            return True
        else:
            return False

    def is_msg_in_logfile(self, filename, msg):
        """Find a line in the ldmsd log file that contains the given msg
        @param msg      message to search for in the log file
        """
        with open(filename, 'r') as loghandle:
            line = loghandle.readline()
            while line:
                if msg in line:
                    return True
                line = loghandle.readline()
            return False

    def getLogMsg(self, len):
        return "strlen(name)=%s" %len

    def test_00_config_file(self):
        name_len = self.getMaxMsgSz("configFile")
        logfile = "config_file.log"
        smp_cfg = self.getGreetingCfgCmd(name_len)
        self.smp = LDMSD(port = self.SMP_PORT, xprt = self.XPRT,
                                cfg = smp_cfg, logfile = logfile,
                                verbose = "DEBUG")
        log.info("starting sampler")
        self.smp.run()
        if not on_timeout(self.is_logfile_ready, filename = logfile):
            raise Exception("ldmsd log file isn't created within 1 second after ldmsd is started.")
        msg = self.getLogMsg(name_len)
        self.assertTrue(on_timeout(self.is_msg_in_logfile, filename = logfile, msg = msg))

    def test_01_ldmsd_controller(self):
        logfile = "ldmsd_controller.log"
        self.smp = LDMSD(port = self.SMP_PORT, xprt = self.XPRT,
                         verbose = "DEBUG", logfile = logfile)
        self.smp.run()
        if not on_timeout(self.is_logfile_ready, filename=logfile):
            raise Exception("ldmsd log file isn't created within 1 second after ldmsd is started.")
        name_len = self.getMaxMsgSz("ldmsd_controller")
        line = self.getGreetingCfgCmd(name_len)
        cfg = tempfile.NamedTemporaryFile()
        cfg.write(line.encode())
        cfg.file.flush()
        # ldmsd_controller subprocess
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT, source = cfg.name)
        ctrl.run()
        msg = self.getLogMsg(name_len)
        self.assertTrue(on_timeout(self.is_msg_in_logfile, filename=logfile, msg = msg))

    def test_02_ldmsctl(self):
        logfile = "ldmsctl.log"
        self.smp = LDMSD(port = self.SMP_PORT, xprt = self.XPRT,
                         verbose = "DEBUG", logfile = logfile)
        self.smp.run()
        if not on_timeout(self.is_logfile_ready, filename=logfile):
            raise Exception("ldmsd log file isn't created within 1 second after ldmsd is started.")
        name_len = self.getMaxMsgSz("ldmsctl")
        line = self.getGreetingCfgCmd(name_len)
        cfg = tempfile.NamedTemporaryFile()
        cfg.write(line.encode())
        cfg.file.flush()
        # ldmsd_controller subprocess
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT,
                                source = cfg.name, ldmsctl = True)
        ctrl.run()
        msg = self.getLogMsg(name_len)
        self.assertTrue(on_timeout(self.is_msg_in_logfile, filename=logfile, msg = msg))

if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_long_config.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

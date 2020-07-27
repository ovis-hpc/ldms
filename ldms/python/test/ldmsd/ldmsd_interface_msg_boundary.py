#!/usr/bin/env python3

# Copyright (c) 2017-2018 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2017-2018 Open Grid Computing, Inc. All rights reserved.
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

# This file contains the test cases for ldmsd message boundary protocol logic in
# ldmsd_controller and ldmsctl

from builtins import str
from builtins import range
import sys
import unittest
from time import sleep
import logging
from ldmsd.ldmsd_util import LDMSD, LDMSD_Controller

log = logging.getLogger(__name__)


class TestLdmsdInterfaceMsgBoundary(unittest.TestCase):
    HOST = "localhost"
    XPRT = "sock"
    PORT = "10001"
    AUTH = "naive"
    LOG = None
    AUTH_OPT = {'uid': "1111", 'gid': "1111"}

    ldmsd = None
    ldmsd_interface = None
    is_ldmsctl = None

    @classmethod
    def setUpClass(cls):
        log.info("Setting up " + cls.__name__)
        try:
            cls.ldmsd = LDMSD(port = cls.PORT, xprt = cls.XPRT,
                              logfile = cls.LOG, auth = cls.AUTH,
                              auth_opt = cls.AUTH_OPT)
            log.info("starting ldmsd")
            cls.ldmsd.run()
            sleep(1)

            cls.ldmsd_interface = LDMSD_Controller(port = cls.PORT, host = cls.HOST,
                                   xprt = cls.XPRT, auth = cls.AUTH,
                                   auth_opt = cls.AUTH_OPT, ldmsctl = cls.is_ldmsctl)
            cls.ldmsd_interface.run()
            cls.ldmsd_interface.read_pty() # Read the welcome message and the prompt
        except:
            if cls.ldmsd:
                del cls.ldmsd
            if cls.ldmsd_interface:
                del cls.ldmsd_interface
            raise
        log.info(cls.__name__ + " set up done")

    @classmethod
    def tearDownClass(cls):
        del cls.ldmsd
        del cls.ldmsd_interface

    def setUp(self):
        log.debug("Testing: {0}".format(self._testMethodName))

    def tearDown(self):
        log.debug("----------------------")

    def _comm(self, cmd):
        cmd = cmd.strip()
        self.ldmsd_interface.write_pty(cmd + "\n")
        resp = self.ldmsd_interface.read_pty()
        lines = resp.splitlines()
        n = len(lines)
        return lines[0:n-1]

    def test_keyword(self):
        resp = self._comm("greeting test")
        self.assertEqual(len(resp), 1)
        self.assertEqual(resp[0], "Hi")
        self.assertTrue(self.ldmsd.is_running())

    def test_cmd_no_resp(self):
        resp = self._comm("greeting")
        self.assertEqual(len(resp), 0)
        self.assertTrue(self.ldmsd.is_running())

    def test_recv_1_rec_resp(self):
        s = "foo"
        resp = self._comm("greeting name={0}".format(s))
        self.assertEqual(len(resp), 1)
        self.assertEqual(resp[0], "Hello '{0}'".format(s))
        self.assertTrue(self.ldmsd.is_running())

    def test_recv_n_rec_resp(self):
        num_rec = 10
        resp = self._comm("greeting level={0}".format(num_rec))
        self.assertEqual(len(resp), num_rec)
        for i in range(0, num_rec):
            self.assertEqual(resp[i], str(i))
        self.assertTrue(self.ldmsd.is_running())

class TestLdmsCtlMsgBoundary(TestLdmsdInterfaceMsgBoundary):
    is_ldmsctl = True

class TestLdmsdControllerMsgBoundary(TestLdmsdInterfaceMsgBoundary):
    is_ldmsctl = False

if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_interface_msg_boundary.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    ldmsctl_suite = unittest.TestLoader().loadTestsFromTestCase(TestLdmsCtlMsgBoundary)
    ldmsd_controller_suite = unittest.TestLoader().loadTestsFromTestCase(TestLdmsdControllerMsgBoundary)
    suite = unittest.TestSuite([ldmsctl_suite, ldmsd_controller_suite])
    result = unittest.TextTestRunner(failfast = True, verbosity = 2).run(suite)
    if not result.wasSuccessful():
        sys.exit(-1)

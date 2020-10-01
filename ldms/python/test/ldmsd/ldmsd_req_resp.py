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

import unittest
from time import sleep
import logging
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request, LDMSD_Req_Attr

log = logging.getLogger(__name__)

class TestLdmsdReqResp(unittest.TestCase):
    AUTH = "naive"
    LOG = None
    AUTH_OPT = {'uid': "1111", 'gid': "1111"}
    SMP_XPRT = "sock"
    SMP_PORT = "10200"
    SMP_NAME = "sampler"
    SMP_HOST = "localhost"
    AGG1_XPRT = "sock"
    AGG1_PORT = "20001"
    AGG1_NAME = "agg1"
    AGG1_HOST = "localhost"
    AGG2_XPRT = "sock"
    AGG2_PORT = "20002"
    AGG2_NAME = "agg2"
    AGG2_HOST = "localhost"

    smp = None
    agg1 = None
    agg2 = None

    @classmethod
    def setUpClass(cls):
        log.info("Setting up " + cls.__name__)
        try:
            cls.smp = LDMSD(port = cls.SMP_PORT, xprt = cls.SMP_XPRT,
                            logfile = cls.LOG, auth = cls.AUTH,
                            auth_opt = cls.AUTH_OPT, host_name = cls.SMP_NAME)
            log.info("starting sampler")
            cls.smp.run()

            agg1_cfg = """
            prdcr_add name=samplerd host=%(host)s xprt=%(xprt)s port=%(port)s type=active interval=20000000
            prdcr_start name=samplerd""" % {
                "host": cls.SMP_HOST,
                "xprt": cls.SMP_XPRT,
                "port": cls.SMP_PORT,
            }

            cls.agg1 = LDMSD(port = cls.AGG1_PORT, xprt = cls.AGG1_XPRT,
                            logfile = cls.LOG, auth = cls.AUTH, cfg = agg1_cfg,
                            auth_opt = cls.AUTH_OPT, host_name = cls.AGG1_NAME)
            log.info("Starting aggregator 1")
            cls.agg1.run()

            agg2_cfg = """
            prdcr_add name=agg1 host=%(host)s xprt=%(xprt)s port=%(port)s \
                    type=active interval=20000000
            prdcr_start name=agg1
            """ % {
                "host": cls.AGG1_HOST,
                "xprt": cls.AGG1_XPRT,
                "port": cls.AGG1_PORT,
            }
            cls.agg2 = LDMSD(port = cls.AGG2_PORT, xprt = cls.AGG2_XPRT,
                            logfile = cls.LOG, auth = cls.AUTH, cfg = agg2_cfg,
                            auth_opt = cls.AUTH_OPT, host_name = cls.AGG2_NAME)
            log.info("Starting aggregator 2")
            cls.agg2.run()
            sleep(1)

        except:
            del cls.smp
            del cls.agg1
            del cls.agg2
            raise
        log.info(cls.__name__ + " set up done")

    @classmethod
    def tearDownClass(cls):
        del cls.smp
        del cls.agg1
        del cls.agg2

    def setUp(self):
        log.debug("Testing: {0}".format(self._testMethodName))

    def tearDown(self):
        log.debug("----------------------")

    def test_comm_1_level_agg(self):
        ctrl = ldmsdInbandConfig(host = self.AGG1_HOST, port = self.AGG1_PORT,
                                     xprt = self.AGG1_XPRT, auth = self.AUTH,
                                     auth_opt = self.AUTH_OPT)
        attr = LDMSD_Req_Attr(value = None, attr_id = LDMSD_Req_Attr.PATH)
        attr_term = LDMSD_Req_Attr()
        req = LDMSD_Request(command_id = LDMSD_Request.GREETING, attrs = [attr, attr_term])
        req.send(ctrl)
        resp = req.receive(ctrl)
        ctrl.close()
        self.assertEqual(len(resp['attr_list']), 1)
        self.assertEqual(resp['attr_list'][0].attr_value.decode(),
                        "{0}:{1}".format(self.SMP_NAME, self.AGG1_NAME))
        self.assertTrue(self.smp.is_running())
        self.assertTrue(self.agg1.is_running())

    def test_comm_2_level_agg(self):
        ctrl = ldmsdInbandConfig(host = self.AGG2_HOST, port = self.AGG2_PORT,
                                     xprt = self.AGG2_XPRT, auth = self.AUTH,
                                     auth_opt = self.AUTH_OPT)
        attr = LDMSD_Req_Attr(value = None, attr_id = LDMSD_Req_Attr.PATH)
        attr_term = LDMSD_Req_Attr()
        req = LDMSD_Request(command_id = LDMSD_Request.GREETING, attrs = [attr, attr_term])
        req.send(ctrl)
        resp = req.receive(ctrl)
        ctrl.close()
        self.assertEqual(len(resp['attr_list']), 1)
        self.assertEqual(resp['attr_list'][0].attr_value.decode(),
                        "{0}:{1}:{2}".format(self.SMP_NAME, self.AGG1_NAME, self.AGG2_NAME))
        self.assertTrue(self.smp.is_running())
        self.assertTrue(self.agg1.is_running())
        self.assertTrue(self.agg2.is_running())

if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "TestLdmsdReqResp.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

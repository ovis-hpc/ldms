#!/usr/bin/env python3

# Copyright (c) 2018 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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
from io import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class TestLDMSDConfig(unittest.TestCase):
    """Test cases focusing on ldms daemon configuration file"""
    LDMSD_UID = os.getuid()
    LDMSD_GID = os.getgid()
    AUTH = "naive"
    LDMSD_AUTH_OPT = {"uid": LDMSD_UID, "gid": LDMSD_GID}
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = None

    # LDMSD instances
    smp = None

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
            time.sleep(2)
            # aggregator will be configured later in the test cases

        except:
            del cls.smp
            raise
        log.info(cls.__name__ + " set up done")

    @classmethod
    def tearDownClass(cls):
        del cls.smp

    def setUp(self):
        log.debug("---- %s ----" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def test_cfg_good(self):
        cfg = """\
            prdcr_add name=%(prdcr)s xprt=%(xprt)s host=%(host)s port=%(port)s \
                      type=active interval=1000000
            prdcr_start name=%(prdcr)s
            updtr_add name=%(updtr)s interval=1000000 offset=500000
            updtr_prdcr_add name=%(updtr)s regex=%(prdcr)s
            updtr_start name=%(updtr)s
        """ % {
            "prdcr": "prdcr",
            "updtr": "updtr",
            "xprt": "sock",
            "host": "localhost",
            "port": self.SMP_PORT,
        }
        daemon = LDMSD(port = "10000", auth = self.AUTH,
                       auth_opt = self.LDMSD_AUTH_OPT,
                       cfg = cfg)
        daemon.run()
        time.sleep(2)
        xprt = ldms.Xprt(self.XPRT, self.AUTH,
                         self.LDMSD_AUTH_OPT)
        xprt.connect("localhost", "10000")
        dir_ = []
        dir_resp = xprt.dir()
        for d in dir_resp:
            dir_.append(d.name)
        self.assertEqual(dir_, ["smp/meminfo"])
        daemon.term()

    def test_cfg_bad(self):
        cfg = """\
            bogus bla=bla
        """
        daemon = LDMSD(port = "10000", auth = self.AUTH,
                       auth_opt = self.LDMSD_AUTH_OPT,
                       cfg = cfg)
        daemon.run()
        time.sleep(2)
        # bad config should terminate the daemon
        self.assertFalse(daemon.is_running())
        daemon.term()

    def test_cfg_semi_bad(self):
        cfg = """\
            prdcr_add name=abc bogus=bogus
        """
        daemon = LDMSD(port = "10000", auth = self.AUTH,
                       auth_opt = self.LDMSD_AUTH_OPT,
                       cfg = cfg)
        daemon.run()
        time.sleep(2)
        # bad config should terminate the daemon
        self.assertFalse(daemon.is_running())
        daemon.term()


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

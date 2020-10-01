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

# Test LDMSD with set array capability

import logging
import unittest
import threading
import time
import re
import os
import shutil

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

# log = logging.getLogger(__name__)

log = None

class Debug(object): pass

DEBUG = Debug()

uS = 1e-6 # usec to sec

ldms.init(512*1024*1024) # 512MB should suffice

class TestLDMSSetArray(unittest.TestCase):
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_GDB_PORT = None # set to "<PORT>" to enable gdbserver
    SMP_LOG = None # set to "<PATH>" to enable logging
    SMP_INT = 100000
    UPD_INT = SMP_INT * 10
    SET_NAME = "smp/meminfo"

    @classmethod
    def setUpClass(cls):
        cls.smp = None
        log.info("--- Setting up TestLDMSSetArray ---")
        try:

            # ldmsd sampler conf
            cfg = """\
            load name=meminfo
            config name=meminfo producer=smp instance=%(name)s \
                                schema=meminfo component_id=1 \
                                set_array_card=20
            start name=meminfo interval=%(interval)d offset=0
            """ % {
                "name": cls.SET_NAME,
                "interval": cls.SMP_INT,
            }

            cls.smp = LDMSD(port=cls.SMP_PORT, xprt=cls.XPRT,
                            cfg = cfg, logfile=cls.SMP_LOG,
                            gdb_port=cls.SMP_GDB_PORT)
            DEBUG.smp = cls.smp
            log.info("starting sampler")
            cls.smp.run()
            time.sleep(1)

            log.info("--- Done setting up TestLDMSSetArray ---")
        except:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        log.info("--- Tearing down TestLDMSSetArray ---")
        del cls.smp

    def _update_cb(self, ldms_set, flags, ctxt):
        self.ts_list.append(ldms_set.transaction_timestamp['sec']+
                            ldms_set.transaction_timestamp['usec']*1e-6)
        if (flags & ldms.LDMS_UPD_F_MORE) == 0:
            self.updating = 0

    def __update(self, s):
        self.updating = 1
        self.ts_list = []
        s.update(cb = self._update_cb)
        time.sleep(0.5) # should be enough to complete
        self.assertEqual(self.updating, 0)
        for a, b in zip(self.ts_list, self.ts_list[1:]):
            DEBUG.a = a
            DEBUG.b = b
            self.assertLess(a, b)
            d = b - a
            self.assertLess(abs(d - self.SMP_INT*uS), 0.001)
        return self.ts_list

    def test_update(self):
        x = ldms.Xprt(name=self.XPRT)
        x.connect(host="localhost", port=self.SMP_PORT)
        s = x.lookup(self.SET_NAME)
        time.sleep(self.UPD_INT*uS)
        log.info("First update ...")
        ts_list0 = self.__update(s)
        time.sleep(self.UPD_INT*uS)
        log.info("Second update ...")
        ts_list1 = self.__update(s)
        ts_list = ts_list0 + ts_list1
        DEBUG.ts_list = ts_list
        DEBUG.ts_list0 = ts_list0
        DEBUG.ts_list1 = ts_list1
        log.info("Verifying data ...")
        for a, b in zip(ts_list, ts_list[1:]):
            DEBUG.a = a
            DEBUG.b = b
            self.assertLess(a, b)
            d = b - a
            self.assertLess(abs(d - self.SMP_INT*uS), 0.001)
        log.info("%d data timestamps verified" % len(ts_list))
        pass


if __name__ == "__main__":
    start = os.getenv("PYTHONSTARTUP")
    if start:
        execfile(start)
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_auth_ovis.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

#!/usr/bin/env python

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

# This file contains test cases for ldmsd set group functionality

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
from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD, LDMSD_Controller
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class Debug(object): pass

DEBUG = Debug()

ldms.ldms_init(128*1024*1024)

class LdmsWrap(object):
    """Conveneint LDMS xprt wrapper"""
    def __init__(self, port, xprt="sock", hostname = "localhost"):
        self.xprt = ldms.LDMS_xprt_new(xprt)
        rc = ldms.LDMS_xprt_connect_by_name(self.xprt, hostname, port)
        assert(rc == 0)
        self.sets = []
        self._dict = {}
        _dirs = ldms.LDMS_xprt_dir(self.xprt)
        for d in _dirs:
            s = ldms.LDMS_xprt_lookup(self.xprt, d,
                                      ldms.LDMS_LOOKUP_BY_INSTANCE)
            self.sets.append(s)
            self._dict[d] = s

    def update(self):
        for _set in self.sets:
            _set.update()

    def get(self, set_name):
        return self._dict.get(set_name)


class TestLDMSDSetGroup(unittest.TestCase):
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = None
    AGG_PORT = "10000"
    AGG_LOG = None
    AGG_GDB_PORT = None
    SMP_GDB_PORT = None
    INTERVAL_US = 1000000
    INTERVAL_SEC = INTERVAL_US / 1e6

    # LDMSD instances
    smp = None
    agg = None

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
                start name=meminfo interval=%(interval_us)d offset=0

                load name=grptest
                config name=grptest producer=smp instance=NA schema=NA \
                       prefix=smp
                config name=grptest members=0xF
                start name=grptest interval=%(interval_us)d offset=0
            """ % {
                "interval_us": cls.INTERVAL_US,
            }
            log.debug("smp_cfg: %s" % smp_cfg)
            cls.smp = LDMSD(port = cls.SMP_PORT, xprt = cls.XPRT,
                            cfg = smp_cfg,
                            logfile = cls.SMP_LOG,
                            gdb_port = cls.SMP_GDB_PORT)
            log.info("starting sampler")
            cls.smp.run()

            # aggregator
            agg_cfg = """
                prdcr_add name=prdcr xprt=%(xprt)s \
                          host=localhost port=%(port)s \
                          interval=%(interval_us)d type=active
                prdcr_start name=prdcr
                updtr_add name=updtr interval=%(interval_us)d offset=%(offset)d\
                          auto_interval=true
                updtr_match_add name=updtr regex=.*/grp match=inst
                updtr_prdcr_add name=updtr regex=prdcr.*
                updtr_start name=updtr
            """ % {
                "xprt": cls.XPRT,
                "port": cls.SMP_PORT,
                "interval_us": cls.INTERVAL_US,
                "offset": int(cls.INTERVAL_US / 2),
            }
            cls.agg = LDMSD(port = cls.AGG_PORT, xprt = cls.XPRT,
                            cfg = agg_cfg,
                            logfile = cls.AGG_LOG,
                            gdb_port = cls.AGG_GDB_PORT)
            log.info("starting aggregator")
            cls.agg.run()
            if cls.AGG_GDB_PORT or cls.SMP_GDB_PORT:
                log.info("AGG_GDB_PORT: %s" % str(cls.AGG_GDB_PORT))
                log.info("SMP_GDB_PORT: %s" % str(cls.SMP_GDB_PORT))
                raw_input("Press ENTER to continue after the gdb is attached")
            time.sleep(2 * cls.INTERVAL_SEC)

        except:
            del cls.agg
            del cls.smp
            raise
        log.info(cls.__name__ + " set up done")

    @classmethod
    def tearDownClass(cls):
        del cls.smp
        del cls.agg

    def setUp(self):
        log.debug("Testing: %s" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def _get_sets(self, port):
        x = ldms.LDMS_xprt_new(self.XPRT)
        rc = ldms.LDMS_xprt_connect_by_name(x, "localhost", port)
        _dirs = ldms.LDMS_xprt_dir(x)
        for d in _dirs:
            pass

    def _verify_smp(self):
        log.info("verifying sampler ...")
        l = LdmsWrap(port = self.SMP_PORT)
        l.update()
        _s0 = { s.instance_name_get(): s.ts_get().sec for s in l.sets }
        time.sleep(2 * self.INTERVAL_SEC)
        l.update()
        _s1 = { s.instance_name_get(): s.ts_get().sec for s in l.sets }
        _d = { k: _s1[k] - _s0[k] for k in _s0 }
        _set_negative = set([k for k in _d if _d[k] < 0])
        _set_zero = set([k for k in _d if _d[k] == 0])
        self.assertEqual(_set_negative, set()) # expect empty set
        self.assertLessEqual(_set_zero, set(["smp/grp", "my/grp"]))
        log.info("sampler verified")

    def _verify_agg(self, uset):
        """Verify that the updated sets in the agg is acually `uset`"""
        log.info("verifying aggregator ...")
        l = LdmsWrap(port = self.AGG_PORT)
        l.update()
        _s0 = { s.instance_name_get(): s.ts_get().sec for s in l.sets }
        time.sleep(3 * self.INTERVAL_SEC)
        l.update()
        _s1 = { s.instance_name_get(): s.ts_get().sec for s in l.sets }
        _d = { k: _s1[k] - _s0[k] for k in _s0 }
        _set = set([k for k in _d if _d[k] > 0])
        self.assertEqual(set(uset), _set)
        log.info("aggregator verified")

    def test_00_verify(self):
        self._verify_smp()
        self._verify_agg(['smp/set0', 'smp/set1', 'smp/set2', 'smp/set3'])

    def test_01_plugin_mod(self):
        # Now, setup the ldmsd_controller subprocess
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT)
        ctrl.run()
        ctrl.read_pty() # discard the welcome message and the prompt
        lines = ctrl.comm_pty("config name=grptest members=0x11")
        self._verify_smp()
        # Now, only set0 and set4 shold be updated on the agg
        self._verify_agg(['smp/set0','smp/set4'])

    def test_02_empty_grp(self):
        """Empty the plugin group so that we can test the manual group"""
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT)
        ctrl.run()
        ctrl.read_pty() # discard the welcome message and the prompt
        lines = ctrl.comm_pty("config name=grptest members=0x0")
        self._verify_smp()
        # Now, only set0 and set4 shold be updated on the agg
        self._verify_agg([])

    def test_03_setgroup_add_ins(self):
        """`setgroup_add` and `setgroup_ins` commands"""
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT)
        ctrl.run()
        ctrl.read_pty() # discard the welcome message and the prompt
        cmd = "setgroup_add name=my/grp interval=%(interval)d" % {
                    "interval": self.INTERVAL_US,
                }
        lines = ctrl.comm_pty(cmd)
        lines = ctrl.comm_pty("setgroup_ins name=my/grp instance=smp/meminfo,smp/set7")
        time.sleep(3 * self.INTERVAL_SEC)
        self._verify_smp()
        self._verify_agg(["smp/meminfo", "smp/set7"])

    def test_04_setgroup_rm(self):
        """`setgroup_rm` test"""
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT)
        ctrl.run()
        ctrl.read_pty() # discard the welcome message and the prompt
        lines = ctrl.comm_pty("setgroup_rm name=my/grp instance=smp/set7")
        time.sleep(3 * self.INTERVAL_SEC)
        self._verify_smp()
        self._verify_agg(["smp/meminfo"])

    def test_05_setgroup_mod(self):
        """`setgroup_mod` test"""
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT)
        ctrl.run()
        ctrl.read_pty() # discard the welcome message and the prompt
        cmd = "setgroup_mod name=my/grp interval=%d" % (60 * self.INTERVAL_US)
        lines = ctrl.comm_pty(cmd)
        time.sleep(3 * self.INTERVAL_SEC)
        self._verify_smp()
        self._verify_agg([])

    def test_06_setgroup_del(self):
        ctrl = LDMSD_Controller(port = self.SMP_PORT, xprt = self.XPRT)
        ctrl.run()
        ctrl.read_pty() # discard the welcome message and the prompt
        cmd = "setgroup_del name=my/grp"
        lines = ctrl.comm_pty(cmd)
        time.sleep(3 * self.INTERVAL_SEC)
        x = LdmsWrap(self.AGG_PORT)
        s = x.get("my/grp")
        self.assertEqual(s, None)


if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_set_group.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = "test_"
    unittest.main(failfast = True, verbosity = 2)

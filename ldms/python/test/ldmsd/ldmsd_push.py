#!/usr/bin/env python

# Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2018 Sandia Corporation. All rights reserved.
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
import pdb
from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD, LDMSD_Controller
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class Debug(object): pass

DEBUG = Debug()

ldms.ldms_init(128*1024*1024)

def all_ts_diff(a, b):
    ka = a.keys()
    kb = b.keys()
    if ka != kb:
        raise ValueError("a.keys() != b.keys()")
    return { k: a[k] - b[k] for k in ka }


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

    def all_ts(self):
        return { s.instance_name_get():(s.ts_get().sec+s.ts_get().usec*1e-6) \
                                                          for s in self.sets }


class TestLDMSDPush(unittest.TestCase):
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = None
    AGG_PORT = "10000"
    AGG_LOG = None
    AGG_GDB_PORT = None
    SMP_GDB_PORT = None
    INTERVAL_US = 1000000
    INTERVAL_SEC = INTERVAL_US * 1e-6
    NUM_SETS = 4
    SMP_PUSH_EVERY = 10 # sampler pushes every X updates

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
                load name=test_sampler
                config name=test_sampler producer=smp \
                       base=set action=default num_sets=%(num_sets)d \
                       push=%(push)d
                start name=test_sampler interval=%(interval_us)d offset=0
            """ % {
                "interval_us": cls.INTERVAL_US,
                "num_sets": cls.NUM_SETS,
                "push": cls.SMP_PUSH_EVERY,
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
                updtr_add name=updtr push=onpush \
                          interval=%(interval_us)d
                updtr_prdcr_add name=updtr regex=prdcr.*
                updtr_start name=updtr
            """ % {
                "xprt": cls.XPRT,
                "port": cls.SMP_PORT,
                "interval_us": cls.INTERVAL_US,
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

    def test_00_verify(self):
        xsmp = LdmsWrap(port = self.SMP_PORT)
        xagg = LdmsWrap(port = self.AGG_PORT)
        xsmp.update()
        s0 = xsmp.all_ts()
        xagg.update()
        a0 = xagg.all_ts()
        time.sleep(2 * self.INTERVAL_SEC)
        xsmp.update()
        s1 = xsmp.all_ts()
        xagg.update()
        a1 = xagg.all_ts()
        ds = all_ts_diff(s1, s0)
        da = all_ts_diff(a1, a0)
        cs = len([ k for k in ds if ds[k] > 0 ])
        ca = len([ k for k in ds if da[k] > 0 ])
        self.assertEqual(cs, self.NUM_SETS)
        self.assertEqual(ca, 0)
        time.sleep((self.SMP_PUSH_EVERY+2) * self.INTERVAL_SEC)
        xagg.update()
        a2 = xagg.all_ts()
        da = all_ts_diff(a2, a1)
        ca = len([ k for k in ds if da[k] > 0 ])
        self.assertEqual(ca, self.NUM_SETS)

if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_push.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = "test_"
    unittest.main(failfast = True, verbosity = 2)

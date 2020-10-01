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

# This file contains test cases for ldmsd set group functionality

from builtins import input
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
import pdb
from io import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD, LDMSD_Controller
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class Debug(object): pass

DEBUG = Debug()

ldms.init(128*1024*1024)

class LdmsSet(object):
    def __init__(self, _s):
        # meta-data
        self.card = _s.card
        self.data_gn = _s.data_gn
        self.data_sz = _s.data_sz
        self.gid = _s.gid
        self.uid = _s.uid
        self.instance_name = _s.instance_name
        self.is_consistent = _s.is_consistent
        self.meta_gn = _s.meta_gn
        self.meta_sz = _s.meta_sz
        self.name = _s.name
        self.perm = _s.perm
        self.producer_name = _s.producer_name
        self.schema_name = _s.schema_name
        self.transaction_duration = _s.transaction_duration
        t = _s.transaction_timestamp
        self.transaction_timestamp = t
        self.transaction_ts = t['sec'] + t['usec']*1e-6
        # metric data
        self.metrics = _s.as_dict()

def get_sets(port, xprt="sock", hostname="localhost"):
    x = ldms.Xprt(name=xprt)
    x.connect(host=hostname, port=port)
    _sets = list()
    _dirs = x.dir()
    for d in _dirs:
        _s = x.lookup(d.name)
        _s.update()
        _sets.append(LdmsSet(_s))
        _s.delete()
    return _sets

def all_ts(_sets):
    return { _s.instance_name: _s.transaction_ts for _s in _sets }

def all_ts_diff(a, b):
    ka = a.keys()
    kb = b.keys()
    if ka != kb:
        raise ValueError("a.keys() != b.keys()")
    return { k: a[k] - b[k] for k in ka }


class TestLDMSDPush(unittest.TestCase):
    XPRT = "sock"
    SMP_PORT = "10200"
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
            smp_cfg = """\
load name=test_sampler
config name=test_sampler producer=smp base=set action=default num_sets=%(num_sets)d push=%(push)d
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
prdcr_add name=prdcr xprt=%(xprt)s host=localhost port=%(port)s interval=%(interval_us)d type=active
prdcr_start name=prdcr
updtr_add name=updtr push=true interval=%(interval_us)d
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
                input("Press ENTER to continue after the gdb is attached")
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
        time.sleep((self.SMP_PUSH_EVERY+2) * self.INTERVAL_SEC)
        _s0 = get_sets(port = self.SMP_PORT)
        s0 = all_ts(_s0)
        _a0 = get_sets(port = self.AGG_PORT)
        a0 = all_ts(_a0)
        time.sleep(2 * self.INTERVAL_SEC)
        _s1 = get_sets(port = self.SMP_PORT)
        s1 = all_ts(_s1)
        _a1 = get_sets(port = self.AGG_PORT)
        a1 = all_ts(_a1)
        ds = all_ts_diff(s1, s0)
        da = all_ts_diff(a1, a0)
        cs = len([ k for k in ds if ds[k] > 0 ])
        ca = len([ k for k in ds if da[k] > 0 ])
        self.assertEqual(cs, self.NUM_SETS)
        self.assertEqual(ca, 0)
        time.sleep((self.SMP_PUSH_EVERY+2) * self.INTERVAL_SEC)
        _a2 = get_sets(port = self.AGG_PORT)
        a2 = all_ts(_a2)
        da = all_ts_diff(a2, a1)
        ca = len([ k for k in ds if da[k] > 0 ])
        self.assertEqual(ca, self.NUM_SETS)

def reraise(self, test, err):
    raise err[1] # re-raise

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
    unittest.TextTestResult.addError = reraise
    unittest.TextTestResult.addFailure = reraise
    unittest.main(failfast = True, verbosity = 2)

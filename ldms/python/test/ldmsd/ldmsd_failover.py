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

# Test LDMSD Failover capability.

import logging
import unittest
import threading
import time
import re
import os
import sys
import shutil
import json

from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

class Debug(object): pass

INTERVAL = 1000000 # sampling, connect, and update interval
XPRT = "sock"
PORT_BASE = 10000 # LV0=10XXX, LV1=11XXX, LV2=12XXX, ...
GDB_PORT_BASE = 20000
VERBOSE = "WARNING"

INTERACTIVE = False

LOGDIR = "log" # daemon logs go in here
try:
    os.makedirs(LOGDIR, 0755)
except:
    pass

DEBUG = Debug()

def iblock(prompt):
    """Interactive block."""
    if not INTERACTIVE:
        return
    if not sys.stdout.isatty():
        return
    raw_input(prompt)

def xfmt(tmp, **kwargs):
    return tmp % kwargs

# Simple sampler template -- works for meminfo, vmstat
SAMP_TEMPLATE = """\
load name=%(name)s
config name=%(name)s instance=%(instance)s producer=%(producer)s
start name=%(name)s interval=%(interval)d offset=%(offset)d
"""

def samp_cfg(name, instance, producer, interval, offset):
    return xfmt(SAMP_TEMPLATE, name = name,
                              instance = instance,
                              producer = producer,
                              interval = interval,
                              offset = offset)


# Simple producer template
PRDCR_TEMPLATE = """\
prdcr_add name=%(name)s xprt=%(xprt)s host=%(host)s port=%(port)s \
          type=active interval=%(interval)d
prdcr_start name=%(name)s
"""

def prdcr_cfg(name, xprt, host, port, interval):
    return xfmt(PRDCR_TEMPLATE, name = name,
                               xprt = xprt,
                               host = host,
                               port = port,
                               interval = interval)


# Simple updater template that updates all producers
UPDTR_TEMPLATE = """\
updtr_add name=%(name)s interval=%(interval)d offset=%(offset)d
updtr_prdcr_add name=%(name)s regex=.*
updtr_start name=%(name)s
"""

def updtr_cfg(name, interval, offset):
    return xfmt(UPDTR_TEMPLATE, name = name,
                               interval = interval,
                               offset = offset)

FAILOVER_TEMPLATE = """\
failover_config host=%(host)s port=%(port)d xprt=%(xprt)s \
                auto_switch=%(auto_switch)s interval=%(interval)d \
"""

def failover_cfg(host, port, xprt, auto_switch, interval, peer_name=None):
    ret = xfmt(FAILOVER_TEMPLATE, host = host,
                                  port = port,
                                  xprt = xprt,
                                  auto_switch = auto_switch,
                                  interval = interval)
    if peer_name:
        ret += " peer_name=" +  peer_name
    return ret + "\n"

def LVX_prdcr(lvl, _id):
    return "lv%d.%02d" % (lvl, _id)

def LVX_port(lvl, _id):
    return PORT_BASE + (1000*lvl) + _id

def LVX_gdb_port(lvl, _id):
    return GDB_PORT_BASE + (1000*lvl) + _id

def LV0_cfg(_id):
    sio = StringIO()
    prdcr = LVX_prdcr(0, _id)
    for smp in ["meminfo", "vmstat"]:
        inst = prdcr + "/" + smp
        cfg = samp_cfg(smp, inst, prdcr, INTERVAL, 0)
        sio.write(cfg)
    return sio.getvalue()

def LVX_cfg(lvl, _id, failover=True):
    # failover only applies to lvl > 0
    if lvl == 0:
        return LV0_cfg(_id)
    sio = StringIO()
    # prdcr
    for xid in (_id*2, _id*2+1):
        prdcr = LVX_prdcr(lvl - 1, xid)
        port = LVX_port(lvl - 1, xid)
        cfg = prdcr_cfg(prdcr, XPRT, "localhost", port, INTERVAL)
        sio.write(cfg)
    # updtr
    cfg = updtr_cfg("updtr", INTERVAL, lvl * INTERVAL / 5)
    sio.write(cfg)
    # failover
    if failover:
        xid = _id ^ 1 # the partner(_id) is (_id bit-wise-xor 1)
        port = LVX_port(lvl, xid)
        peer_name = LVX_prdcr(lvl, xid)
        cfg = failover_cfg("localhost", port, XPRT, 1, INTERVAL, peer_name)
        sio.write(cfg)
    return sio.getvalue()

def LVX_ldmsd_new(lvl, _id, failover = True, gdb = False):
    cfg = LVX_cfg(lvl, _id, failover)
    prdcr = LVX_prdcr(lvl, _id)
    logfile = LOGDIR + "/" + prdcr
    port = LVX_port(lvl, _id)
    gdb_port = LVX_gdb_port(lvl, _id) if gdb else None
    ldmsd = LDMSD(port = port, xprt = XPRT, logfile = logfile,
                  cfg = cfg, gdb_port = gdb_port,
                  name = prdcr,
                  verbose = VERBOSE)
    return ldmsd

class TestLDMSDFailover(unittest.TestCase):
    LEVELS = 3

    ldmsds = dict()

    @classmethod
    def ldmsd_iter(cls):
        for lv in range(0, cls.LEVELS):
            N = 2 ** (cls.LEVELS - 1 - lv)
            for _id in range(0, N):
                yield(lv, _id)

    @classmethod
    def setUpClass(cls):
        log.info("Setting up class %s" % cls.__name__)
        for lvl, _id in cls.ldmsd_iter():
            usegdb = False
            ldmsd = LVX_ldmsd_new(lvl, _id, lvl < (cls.LEVELS - 1), usegdb)
            log.info("starting %s" % LVX_prdcr(lvl, _id))
            ldmsd.run()
            cls.ldmsds[(lvl, _id)] = ldmsd
        time.sleep(3) # Everything should be up by now.
        iblock("\nPress ENTER to begin ...")
        log.info("Done setting up %s" % cls.__name__)

    @classmethod
    def tearDownClass(cls):
        iblock("\nPress ENTER to end ...")
        del cls.ldmsds

    def __verify(self, lvl, _id, failover=False):
        prdcr = LVX_prdcr(lvl, _id)
        log.info("Verifying %s" % prdcr)
        port = LVX_port(lvl, _id)
        x = ldms.LDMS_xprt_new(XPRT)
        rc = ldms.LDMS_xprt_connect_by_name(x, "localhost", str(port))
        DEBUG.x = x
        DEBUG.rc = rc
        assert(rc == 0)
        s0 = set()
        N = 2**lvl
        off = N * _id
        s0 = set([LVX_prdcr(0, i) + "/" + s \
                        for i in range(off, off + N) \
                        for s in ["meminfo", "vmstat"]
                 ])
        if failover:
            off = N * (_id ^ 1)
            s0.update([LVX_prdcr(0, i) + "/" + s \
                            for i in range(off, off + N) \
                            for s in ["meminfo", "vmstat"]
                    ])
        dirs = ldms.LDMS_xprt_dir(x)
        s1 = set(dirs)
        DEBUG.s0 = s0
        DEBUG.s1 = s1
        self.assertEqual(s0, s1)

    def test_00_verify(self):
        for lv, _id in self.ldmsd_iter():
            self.__verify(lv, _id)

    def test_01_lv1_failover(self):
        dead = (1, 1)
        ldmsd = self.ldmsds[dead]
        prdcr = LVX_prdcr(dead[0], dead[1])
        iblock("\nPress ENTER to terminate %s" % prdcr)
        ldmsd.term()
        time.sleep(3 * (INTERVAL/1000000.0))
        for lv, _id in self.ldmsd_iter():
            if (lv, _id) == dead:
                continue
            is_failover = (lv == dead[0] and _id == (dead[1]^0x1))
            self.__verify(lv, _id, is_failover)

    def test_02_lv1_failback(self):
        idx = (1, 1)
        iblock("\nPress ENTER to resurrect %s" % LVX_prdcr(*idx))
        # resurrect the dead
        ldmsd = LVX_ldmsd_new(*idx)
        self.ldmsds[idx] = ldmsd
        log.info("Resurrecting %s" % LVX_prdcr(*idx))
        ldmsd.run()
        time.sleep(4)
        iblock("\nPress ENTER to start verifying ...")
        for lv, _id in self.ldmsd_iter():
            self.__verify(lv, _id, False)

    def test_03_lv1_manual_failover(self):
        iblock("\nPress ENTER to manually failover (lv1.01) ...")
        idx = (1, 1)
        ctrl = ldmsdInbandConfig(host = "localhost",
                                 port = LVX_port(*idx),
                                 xprt = XPRT)
        ctrl.comm("failover_mod", auto_switch = 0)
        ctrl.comm("failover")
        ctrl.close()
        time.sleep(1)
        self.__verify(*idx, failover=True)

    def test_04_lv1_manual_failback(self):
        iblock("\nPress ENTER to manually failback (lv1.01) ...")
        idx = (1, 1)
        ctrl = ldmsdInbandConfig(host = "localhost",
                                 port = LVX_port(*idx),
                                 xprt = XPRT)
        ctrl.comm("failback")
        ctrl.close()
        time.sleep(1)
        self.__verify(*idx, failover=False)

    def test_05_bad_pair(self):
        port = LVX_port(1, 0)
        pname = LVX_prdcr(1, 0)
        cfg = """\
        failover_config host=localhost port=%(port)d xprt=%(xprt)s \
                        auto_switch=1 interval=1000000 \
                        peer_name=%(name)s
        """ % {
            "port": port,
            "xprt": XPRT,
            "name": pname,
        }
        p = LDMSD(9999, cfg = cfg, name = "bad")
        p.run()
        time.sleep(2)
        ctrl = ldmsdInbandConfig(host = "localhost", port = 9999, xprt = XPRT)
        resp = ctrl.comm("failover_status")
        DEBUG.resp = resp
        ctrl.close()
        obj = json.loads(resp['msg'])
        self.assertEqual(int(obj['flags']['AX_CONNECTED']), 0)

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
            filename = "ldmsd_failover.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.TestLoader.testMethodPrefix = "test_"
    unittest.main(failfast = True, verbosity = 2)

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

from builtins import object, zip
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

class TestLDMSDSetArray(unittest.TestCase):
    XPRT = "sock"
    SMP_PORT = "10001"
    AGG_PORT = "10000"
    AGG2_PORT = "9999"
    SMP_INT = 100000
    AGG_INT = SMP_INT * 5
    SMP_GDB_PORT = None # set "<PORT>" to enable gdbserver on sampler
    AGG_GDB_PORT = None # set "<PORT>" to enable gdbserver on aggregator
    AGG2_GDB_PORT = None # set "<PORT>" to enable gdbserver on aggregator
    # NOTE: use `gdb> target remote :<PORT>` to connect to the gdbserver
    SMP_LOG = None # set "<PATH>" to enable sampler logging
    AGG_LOG = None # set "<PATH>" to enable aggregator logging
    AGG2_LOG = None # set "<PATH>" to enable aggregator logging

    @classmethod
    def setUpClass(cls):
        cls.smp = None
        cls.agg = None
        log.info("--- Setting up TestLDMSDSetArray ---")
        try:

            # ldmsd sampler conf
            cfg = """\
            load name=meminfo
            config name=meminfo producer=smp instance=smp/meminfo \
                                schema=meminfo component_id=1 \
                                set_array_card=20 \
                                uid=0 gid=0 perm=0777
            start name=meminfo interval=%(interval)d offset=0
            """ % {
                "interval": cls.SMP_INT,
            }

            cls.smp = LDMSD(port=cls.SMP_PORT, cfg = cfg, logfile=cls.SMP_LOG,
                            gdb_port=cls.SMP_GDB_PORT)
            DEBUG.smp = cls.smp
            log.info("starting sampler")
            cls.smp.run()
            time.sleep(1)

            log.info("--- Done setting up TestLDMSDSetArray ---")
        except:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        log.info("--- Tearing down TestLDMSDSetArray ---")
        del cls.smp

    def test_lv1_store(self):
        # ldmsd_aaggregator conf
        shutil.rmtree("csv/csv1", ignore_errors=True)
        os.makedirs("csv/csv1")
        cfg = """\
        prdcr_add name=prdcr xprt=%(xprt)s host=localhost port=%(port)s \
                  interval=1000000 type=active
        prdcr_start name=prdcr

        updtr_add name=updtr interval=%(interval)d offset=50000
        updtr_prdcr_add name=updtr regex=prdcr
        updtr_start name=updtr

        load name=store_csv
        config name=store_csv path=csv buffer=0
        strgp_add name=strgp plugin=store_csv container=csv1 schema=meminfo
        strgp_prdcr_add name=strgp regex=prdcr
        strgp_start name=strgp
        """ % {
            "xprt": self.XPRT,
            "port": self.SMP_PORT,
            "interval": self.AGG_INT,
        }
        agg = LDMSD(port=self.AGG_PORT, cfg=cfg, logfile=self.AGG_LOG,
                        gdb_port=self.AGG_GDB_PORT)
        #DEBUG.agg = agg
        log.info("starting aggregator")
        agg.run()
        log.info("collecting data")
        time.sleep(2 + 2*self.AGG_INT*uS)
        #agg.term()
        time.sleep(0.25)
        log.info("Verifying Data")
        # expecting to see a bunch of data, with dt ~ self.SMP_INT usec
        f = open("csv/csv1/meminfo")
        lines = f.readlines()
        f.close()
        lines = lines[1:] # the [0] is the header
        rexp = re.compile("^(\d+\.\d+),.*$")
        ts = [ float(rexp.match(l).group(1)) for l in lines ]
        for a, b in zip(ts, ts[1:]):
            dt = b - a
            # allowing 1 millisec error
            self.assertLess(abs(dt - self.SMP_INT*uS), 0.001)
        log.info("%d data timestamps verified" % len(ts))

    def test_lv2_store(self):
        # ldmsd_aaggregator conf
        shutil.rmtree("csv/csv2", ignore_errors=True)
        os.makedirs("csv/csv2")
        cfg = """\
        prdcr_add name=prdcr xprt=%(xprt)s host=localhost port=%(port)s \
                  interval=1000000 type=active
        prdcr_start name=prdcr

        updtr_add name=updtr interval=%(interval)d offset=50000
        updtr_prdcr_add name=updtr regex=prdcr
        updtr_start name=updtr
        """ % {
            "xprt": self.XPRT,
            "port": self.SMP_PORT,
            "interval": self.AGG_INT,
        }
        agg = LDMSD(port=self.AGG_PORT, cfg=cfg, logfile=self.AGG_LOG,
                        gdb_port=self.AGG_GDB_PORT)
        DEBUG.agg = agg
        log.info("starting aggregator-1")
        agg.run()
        cfg = """\
        prdcr_add name=prdcr xprt=%(xprt)s host=localhost port=%(port)s \
                  interval=1000000 type=active
        prdcr_start name=prdcr

        updtr_add name=updtr interval=%(interval)d offset=50000
        updtr_prdcr_add name=updtr regex=prdcr
        updtr_start name=updtr

        load name=store_csv
        config name=store_csv path=csv buffer=0
        strgp_add name=strgp plugin=store_csv container=csv2 schema=meminfo
        strgp_prdcr_add name=strgp regex=prdcr
        strgp_start name=strgp
        """ % {
            "xprt": self.XPRT,
            "port": self.AGG_PORT,
            "interval": self.AGG_INT,
        }
        agg2 = LDMSD(port=self.AGG2_PORT, cfg=cfg, logfile=self.AGG2_LOG,
                        gdb_port=self.AGG2_GDB_PORT)
        #DEBUG.agg2 = agg2
        log.info("starting aggregator-2")
        agg2.run()
        log.info("collecting data")
        time.sleep(2 + 3*self.AGG_INT*uS)
        agg.term()
        agg2.term()
        time.sleep(0.25)
        log.info("Verifying Data")
        # expecting to see a bunch of data, with dt ~ self.SMP_INT usec
        f = open("csv/csv2/meminfo")
        lines = f.readlines()
        f.close()
        lines = lines[1:] # the [0] is the header
        rexp = re.compile("^(\d+\.\d+),.*$")
        ts = [ float(rexp.match(l).group(1)) for l in lines ]
        for a, b in zip(ts, ts[1:]):
            dt = b - a
            # allowing 1 millisec error
            self.assertLess(abs(dt - self.SMP_INT*uS), 0.001)
        log.info("%d data timestamps verified" % len(ts))


if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_set_array.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

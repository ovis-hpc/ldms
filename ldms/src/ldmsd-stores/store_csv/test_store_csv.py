#!/usr/bin/env python

# Copyright (c) 2019 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2019 Open Grid Computing, Inc. All rights reserved.
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

# This file contains test cases for ldmsd store_sos

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
import socket
import shutil
import sys
import pdb
import collections
from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request
import csv

DIR = "test_store_csv" if not sys.path[0] else sys.path[0] + "/test_store_csv"

log = logging.getLogger(__name__)
HOSTNAME = socket.gethostname()

ldms.ldms_init(512*1024*1024) # 512MB should suffice

class Debug(object):
    pass

D = Debug()

def ldms_set_as_dict(_set):
    return { _set.metric_name_get(_mid): _val \
                                for (_mid, _val) in _set.iter_items() }

def ldms_set_as_tuple(_set):
    t = tuple( _val for (_mid, _val) in _set.iter_items() )
    # add special fields
    ts = _set.ts_get()
    ts_float = ts.sec + ts.usec*1e-6
    prdcr = _set.producer_name_get()
    return (ts_float, ts.usec, prdcr) + t

class Hdr(object):
    __slots__ = ['name', 'idx', 'type', '_val_fn']
    LONG=1
    FLOAT=2
    STR=3
    TYPE_MAP = {
        's': LONG,
        'u': LONG,
        'f': FLOAT,
        'd': FLOAT,
        'c': STR,
    }
    VAL_MAP = {
        LONG: long,
        FLOAT: float,
        STR: str,
    }

    def __init__(self, name=0, idx=-1):
        self.name = name
        self.idx = idx
        # the name determine the type in this test case
        try:
            if "_id" in name:
                self.type = self.LONG
            elif "_usec" in name:
                self.type = self.LONG
            elif "Time" == name:
                self.type = self.FLOAT
            else:
                self.type = self.TYPE_MAP[name[0]]
        except:
            self.type = self.STR
        self._val_fn = self.VAL_MAP[self.type]

    def parseValue(self, val):
        return self._val_fn(val)


def unique(lst):
    """Unique elements -- keeping the same order"""
    s = set()
    l = list()
    for x in lst:
        if x not in s:
            s.add(x)
            l.append(x)
    return l


class LdmsCsv(list):
    RE = re.compile(r'#?([^[]*)(\[\])?(?:\.(\d+))?')
    def __init__(self, path):
        rdr = csv.reader(open(path, "r"))
        hdr = next(rdr)
        self.hdr = self.parseHdr(hdr)
        self.Row = collections.namedtuple('Row', unique([x.name \
                                                        for x in self.hdr]))
        for row in rdr:
            _row = self.parseRow(row)
            self.append(_row)

    def parseHdr(self, hdr):
        phdr = list()
        for h in hdr:
            m = self.RE.match(h)
            if not m:
                raise ValueError('Bad header format')
            (name, arr, idx) = m.groups()
            if idx == None:
                idx = -1
            else:
                idx = int(idx)
            if arr:
                name += "_array"
            idx = -1 if idx == None else int(idx)
            phdr.append(Hdr(name, idx))
        return phdr

    def parseRow(self, row):
        # self.hdr contains parsed headers
        # self.Row is the namedtuple for each row
        vals = list()
        name = None
        cv = None
        for h, x in zip(self.hdr, row):
            v = h.parseValue(x)
            if h.name == name:
                cv.append(v)
                assert(len(cv) == h.idx + 1)
            else:
                if cv != None:
                    # commit
                    if type(cv) == list:
                        cv = tuple(cv)
                    vals.append(tuple(cv) if type(cv) == list else cv)
                if h.idx == -1:
                    cv = v
                else:
                    cv = [v]
            name = h.name
        vals.append(tuple(cv) if type(cv) == list else cv) # commit last cv
        return self.Row(*vals)


class TestStoreCsv(unittest.TestCase):
    """Test cases for ldmsd store_csv plugin"""
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = DIR + "/smp.log" # for debugging
    AGG_PORT = "11001"
    AGG_LOG = DIR + "/agg.log" # for debugging
    PRDCR = HOSTNAME + ":" + SMP_PORT
    CSV_PATH = DIR + "/csv"

    # LDMSD instances
    smp = None
    agg = None

    @classmethod
    def setUpClass(cls):
        if os.path.exists(cls.CSV_PATH):
            os.remove(cls.CSV_PATH)

        try:
            smpcfg = """
                load name=test plugin=test_sampler
                config name=test component_id=100
                config name=test action=add_all metric_array_sz=4 schema=sch
                config name=test action=add_set schema=sch instance=test

                smplr_add name=smp_test instance=test interval=1000000 offset=0
                smplr_start name=smp_test
            """
            cls.smp = LDMSD(port = cls.SMP_PORT, cfg = smpcfg,
                            logfile = cls.SMP_LOG)
            cls.smp.run()
            time.sleep(2.0)

            aggcfg = """
                load name=csv plugin=store_csv
                config name=csv path=%(CSV_PATH)s

                prdcr_add name=smp xprt=%(XPRT)s host=localhost \
                          port=%(SMP_PORT)s \
                          type=active interval=1000000
                prdcr_start name=smp

                updtr_add name=upd interval=1000000 offset=500000
                updtr_prdcr_add name=upd regex=.*
                updtr_start name=upd

                strgp_add name=strgp container=csv schema=sch
                strgp_prdcr_add name=strgp regex=.*
                strgp_start name=strgp
            """ % vars(cls)
            cls.agg = LDMSD(port = cls.AGG_PORT, cfg = aggcfg,
                            logfile = cls.AGG_LOG)
            cls.agg.run()
            time.sleep(4.0) # make sure that it starts storing something
        except:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        if cls.smp:
            del cls.smp
        if cls.agg:
            del cls.agg

    def setUp(self):
        log.debug("---- %s ----" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def test_01_verify(self):
        """Verify data in the storage"""
        x = ldms.LDMS_xprt_new(self.XPRT)
        rc = ldms.LDMS_xprt_connect_by_name(x, "localhost", self.SMP_PORT)
        if rc:
            log.error("rc: %d" % rc)
        assert(rc == 0)
        dlist = ldms.LDMS_xprt_dir(x)
        _sets = []
        log.info("Looking up sets")
        self.assertEqual(len(dlist), 1)
        s = ldms.LDMS_xprt_lookup(x, dlist[0], 0)
        log.info("Collecting data from LDMS for comparison")
        data = set()
        for i in range(0, 10):
            # update first
            s.update()
            l = ldms_set_as_tuple(s)
            data.add(l)
            time.sleep(1)
        time.sleep(1) # to make sure that the last data point has been stored
        log.info("Verifying...")
        csv_data = LdmsCsv("test_store_csv/csv")
        csv_data = set(csv_data)
        self.assertLessEqual(data, csv_data)


if __name__ == "__main__":
    startup = os.getenv("PYTHONSTARTUP")
    if startup:
        execfile(startup)
    if not os.path.exists(DIR):
        os.makedirs(DIR)
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = DIR + "/test_store_sos.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

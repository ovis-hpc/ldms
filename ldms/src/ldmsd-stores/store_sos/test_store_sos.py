#!/usr/bin/env python

# Copyright (c) 2018-2019 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2018-2019 Open Grid Computing, Inc. All rights reserved.
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
import pdb
from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request
from sosdb import Sos

log = logging.getLogger(__name__)
HOSTNAME = socket.gethostname()

ldms.ldms_init(512*1024*1024) # 512MB should suffice

def ldms_set_as_dict(_set):
    return { _set.metric_name_get(_mid): _val \
                                for (_mid, _val) in _set.iter_items() }

def ldms_set_as_tuple(_set, with_ts = False):
    t = tuple( _val for (_mid, _val) in _set.iter_items() )
    if with_ts:
        ts = _set.ts_get()
        ts = (ts.sec, ts.usec)
        t = (ts,) + t
    return t

def sos_schema_obj_iter(sch, attr=None):
    if not attr:
        for attr in sch.attr_iter():
            if attr.is_indexed():
                break
    if type(attr) == int:
        attr = sch.attr_by_id(attr)
    if type(attr) == str:
        attr = sch.attr_by_name(attr)
    itr = attr.attr_iter()
    ok = itr.begin()
    while ok:
        obj = itr.item()
        yield obj
        ok = itr.next()

def sos_objs(path, schema):
    cont = Sos.Container(path = path)
    sch = cont.schema_by_name(schema)
    return [ tuple( v for v in obj ) for obj in sos_schema_obj_iter(sch) ]

class TestStoreSos(unittest.TestCase):
    """Test cases for ldmsd store_sos plugin"""
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = None # for debugging
    AGG_PORT = "11001"
    AGG_LOG = None # for debugging
    PRDCR = HOSTNAME + ":" + SMP_PORT
    SOS_PATH = "sos/test_store_sos"

    # LDMSD instances
    smp = None
    agg = None

    @classmethod
    def setUpClass(cls):
        if os.path.exists(cls.SOS_PATH):
            shutil.rmtree(cls.SOS_PATH)
        os.makedirs(cls.SOS_PATH)

        try:
            smpcfg = """
                load name=test plugin=test_sampler
                config name=test component_id=100
                config name=test action=default

                smplr_add name=smp_test instance=test interval=1000000 offset=0
                smplr_start name=smp_test
            """
            cls.smp = LDMSD(port = cls.SMP_PORT, cfg = smpcfg,
                            logfile = cls.SMP_LOG)
            cls.smp.run()
            time.sleep(2.0)

            aggcfg = """
                load name=sos plugin=store_sos
                config name=sos path=%(sos_path)s

                prdcr_add name=smp xprt=%(xprt)s host=localhost port=%(port)s \
                          type=active interval=1000000
                prdcr_start name=smp

                updtr_add name=upd interval=1000000 offset=500000
                updtr_prdcr_add name=upd regex=.*
                updtr_start name=upd

                strgp_add name=strgp container=sos schema=test_sampler
                strgp_prdcr_add name=strgp regex=.*
                strgp_start name=strgp
            """ % {
                "sos_path": cls.SOS_PATH,
                "xprt": cls.XPRT,
                "port": cls.SMP_PORT,
            }
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
        for name in dlist:
            s = ldms.LDMS_xprt_lookup(x, name, 0)
            assert(s)
            _sets.append(s)
        log.info("Collecting data from LDMS for comparison")
        data = set()
        for i in range(0, 10):
            # update first
            for s in _sets:
                s.update()
            for s in _sets:
                l = ldms_set_as_tuple(s, with_ts = True)
                data.add(l)
                dlen = len(l)
            time.sleep(1)
        time.sleep(1) # to make sure that the last data point has been stored
        log.info("Verifying...")
        objs = set( o[:dlen] for o in sos_objs(self.SOS_PATH, "test_sampler") )
        ldata = list(data) # for debug
        sdata = list(objs) # for debug
        self.assertLessEqual(data, objs) # data \subset objs


if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    if not os.path.exists('log'):
        os.mkdir('log')
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "log/test_store_sos.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

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

# This file contains test cases for ldmsd store_amqp

import os
import re
import sys
import time
import json
import shutil
import socket
import logging
import unittest
import threading
import subprocess

import pdb

from threading import Thread
from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

import pika # amqp client library

DIR = "test_store_amqp" if not sys.path[0] else sys.path[0] + "/test_store_amqp"

log = logging.getLogger(__name__)
HOSTNAME = socket.gethostname()

class Debug(object):
    pass

D = Debug()

ldms.ldms_init(512*1024*1024) # 512MB should suffice

def tuple_from_dict(d, keys = None):
    if not keys:
        keys = d.keys()
    return tuple( d[k] for k in keys )

def ldms_set_as_dict(_set):
    ts = _set.ts_get()
    d = {
        "instance_name": _set.instance_name_get(),
        "schema_name": _set.schema_name_get(),
        "timestamp": ts.sec + ts.usec*1e-6,
    }
    d.update({ _set.metric_name_get(_mid): _val \
                for (_mid, _val) in _set.iter_items() })
    return d

def ldms_set_as_tuple(_set):
    t = tuple( _val for (_mid, _val) in _set.iter_items() )
    ts = _set.ts_get()
    ts_float = ts.sec + ts.usec*1e-6
    t = (_set.instance_name_get(), ts_float,) + t
    return t

def as_tuple(x):
    if type(x) == list:
        return tuple(x)
    return x

class AMQPSink(object):
    def __init__(self, exchange, routing_key):
        self.conn = pika.BlockingConnection(pika.ConnectionParameters('localhost'))
        self.ch = self.conn.channel()
        result = self.ch.queue_declare(exclusive = True)
        self.ch.queue_bind(exchange=exchange, queue=result.method.queue,
                           routing_key=routing_key)
        self.ch.basic_consume(self._recv, queue=result.method.queue)
        self.thread = Thread(target = self)
        self.data = list()
        self.raw = list()

    def start(self):
        self.is_active = True
        self.thread.start()

    def stop(self):
        self.is_active = False
        self.thread.join()

    def _recv(self, ch, method, properties, body):
        """AMQP receive callback"""
        self.raw.append(body)
        obj = json.loads(body)
        d = { k: as_tuple(obj[k]) for k in obj if k != "metrics" }
        m = obj["metrics"]
        d.update( { k: as_tuple(m[k]) for k in m } )
        self.data.append(d)

    def __call__(self, *args, **kwargs):
        while self.is_active:
            self.conn.process_data_events(time_limit = 1) # blocking 1s max
        self.ch.stop_consuming()

    def __del__(self):
        if self.is_active:
            self.stop()


class TestStoreAmqp(unittest.TestCase):
    """Test cases for ldmsd store_amqp plugin"""
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = DIR + "/smp.log" # for debugging
    AGG_PORT = "11001"
    AGG_LOG = DIR + "/agg.log" # for debugging
    PRDCR = HOSTNAME + ":" + SMP_PORT

    # LDMSD instances
    smp = None
    agg = None

    amqp_sink = None

    @classmethod
    def setUpClass(cls):
        try:
            smpcfg = """
                load name=test plugin=test_sampler
                config name=test component_id=100
                config name=test action=add_all metric_array_sz=4 \
                       schema=test
                config name=test action=add_set schema=test instance=test

                smplr_add name=smp_test instance=test interval=1000000 offset=0
                smplr_start name=smp_test
            """
            cls.smp = LDMSD(port = cls.SMP_PORT, cfg = smpcfg,
                            logfile = cls.SMP_LOG)
            cls.smp.run()
            time.sleep(2.0)

            aggcfg = """
                load name=amqp plugin=store_amqp
                config name=amqp host=localhost

                prdcr_add name=smp xprt=%(XPRT)s host=localhost \
                          port=%(SMP_PORT)s type=active interval=1000000
                prdcr_start name=smp

                updtr_add name=upd interval=1000000 offset=500000
                updtr_prdcr_add name=upd regex=.*
                updtr_start name=upd

                strgp_add name=strgp container=amqp schema=test
                strgp_prdcr_add name=strgp regex=.*
                strgp_start name=strgp
            """ % vars(cls)
            cls.agg = LDMSD(port = cls.AGG_PORT, cfg = aggcfg,
                            logfile = cls.AGG_LOG)
            cls.agg.run()
            time.sleep(4.0) # make sure that it starts storing something
            cls.amqp_sink = AMQPSink("LDMS.set.test", "JSON")
            cls.amqp_sink.start()
        except:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        if cls.smp:
            del cls.smp
        if cls.agg:
            del cls.agg
        if cls.amqp_sink:
            cls.amqp_sink.stop()
            del cls.amqp_sink

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
        self.assertEqual(len(dlist), 1)
        log.info("Looking up sets")
        _set = ldms.LDMS_xprt_lookup(x, dlist[0], 0)
        assert(_set)
        log.info("Collecting data from LDMS for comparison")
        data = []
        for i in range(0, 10):
            # update first
            _set.update()
            d = ldms_set_as_dict(_set)
            data.append(d)
            time.sleep(1)
        time.sleep(1) # to make sure that the last data point has been stored
        log.info("Verifying...")
        keys = data[0].keys()
        for d in data:
            self.assertEqual(set(keys), set(d.keys()))
        for d in self.amqp_sink.data:
            self.assertEqual(set(keys), set(d.keys()))
        data = set( tuple_from_dict(d, keys) for d in data )
        amqp_data = set( tuple_from_dict(d, keys) for d in self.amqp_sink.data )
        self.assertGreater(len(data), 0)
        self.assertLessEqual(data, amqp_data)


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
            filename = DIR + "/test_store_amqp.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

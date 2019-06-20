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

# This file contains test cases for ldmsd store_function_csv

import sys
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
import csv
from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

DIR = sys.path[0] + "/test_store_function_csv"
if not os.path.exists(DIR):
    os.makedirs(DIR)

log = logging.getLogger(__name__)
HOSTNAME = socket.gethostname()

ldms.ldms_init(512*1024*1024) # 512MB should suffice

class Debug(object): pass

D = Debug() # Debug info holder

#########################################
#-- CSV function processing utilities --#
#########################################

class Enum(object):
    def __init__(self, v, s):
        self.value = v
        self.str = s

    def __str__(self):
        return self.str

    def __repr__(self):
        return "(%s, %d)" % (self.str, self.value)

    def to_int(self):
        return self.val

# Fn for holding ENUM
class Fn(object):
    def __setattr__(self, attr, value):
        self.__dict__[attr] = Enum(value, attr)

FN = Fn()

FN.MAP = 0
FN.REDUCE = 1
FN.DELTA = 2
FN.RATE = 3
FN.THRESH = 4

FN_TBL = {
    "delta": (FN.DELTA, lambda new, old: new - old),
    "rate": (FN.RATE, lambda new, old, time: long((new-old)/time)),

    "min_n": (FN.MAP, lambda *args: min(args)),
    "max_n": (FN.MAP, lambda *args: max(args)),
    "sum_n": (FN.MAP, lambda *args: sum(args)),
    "avg_n": (FN.MAP, lambda *args: long(sum(args)/len(args))),
    "sub_ab": (FN.MAP, lambda a, b: a - b),
    "mul_ab": (FN.MAP, lambda a, b: a * b),
    "div_ab": (FN.MAP, lambda a, b: long(a / b)),
    "sum_vs": (FN.MAP, lambda a, b: a + b),
    "sub_vs": (FN.MAP, lambda a, b: a - b),
    "sub_sv": (FN.MAP, lambda a, b: a - b),
    "mul_vs": (FN.MAP, lambda a, b: a * b),
    "div_vs": (FN.MAP, lambda a, b: a / b),
    "div_sv": (FN.MAP, lambda a, b: a / b),

    "thresh_ge": (FN.THRESH, lambda a, b: a >= b),
    "thresh_lt": (FN.THRESH, lambda a, b: a < b),

    "raw": (FN.MAP, lambda x: x),
    "":    (FN.MAP, lambda x: x),

    "min": (FN.REDUCE, lambda *args: min(args)),
    "max": (FN.REDUCE, lambda *args: max(args)),
    "sum": (FN.REDUCE, lambda *args: sum(args)),
    "avg": (FN.REDUCE, lambda *args: long(sum(args)/len(args))),
}

class Expr(object):
    __slots__ = ['expr', 'fn', 'fn_type', 'vars', '_eval']
    rexp = re.compile(r'^([^(]+)\((.*)\)$')
    def __init__(self, expr):
        m = self.rexp.match(expr)
        if m:
            (fn_key, self.vars) = m.groups()
            self.vars = self.vars.split(',')
        else:
            fn_key = ""
            self.vars = [expr]
        self.vars = map(lambda s: s.strip(), self.vars)
        (self.fn_type, self.fn) = FN_TBL[fn_key]
        self._eval = self.eval_tbl[self.fn_type]

    def _eval_map(self, curr_row, prev_row):
        return vmap(self.fn, *[curr_row[x].val for x in self.vars])

    def _eval_delta(self, curr_row, prev_row):
        return vmap(self.fn, curr_row[self.vars[0]].val,
                             prev_row[self.vars[0]].val)

    def _eval_rate(self, curr_row, prev_row):
        return vmap(self.fn, curr_row[self.vars[0]].val,
                             prev_row[self.vars[0]].val, curr_row["DT"].val)

    def _eval_reduce(self, curr_row, prev_row):
        return self.fn(*curr_row[self.vars[0]].val)

    def _eval_thresh(self, curr_row, prev_row):
        return vmap(self.fn, curr_row[self.vars[0]].val, long(self.vars[1]))

    eval_tbl = {
        FN.MAP    : _eval_map,
        FN.DELTA  : _eval_delta,
        FN.RATE   : _eval_rate,
        FN.REDUCE : _eval_reduce,
        FN.THRESH : _eval_thresh,
    }

    def eval(self, curr_row, prev_row):
        x = self._eval(self, curr_row, prev_row)
        md = max( [curr_row[v].dim for v in self.vars if v in curr_row] )
        if type(x) == list:
            if md == 0:
                return x[0]
            else:
                return tuple(x)
        return x


class Result(object):
    __slots__ = ['desc', 'val', 'flag', 'dim', 'expr']
    def __init__(self, desc, expr=None, dim=0, val=None, flag=None):
        # dim 0 means scalar
        self.desc = desc
        if not expr:
            self.expr = Expr(desc)
        self.val = val if val else list()
        self.flag = flag
        self.dim = dim

    def __str__(self):
        return repr(self)

    def __repr__(self):
        return "(%(desc)s, %(dim)s, %(val)s, %(flag)s)" % \
                { k:str(self.__getattribute__(k)) for k in self.__slots__ }

FLAG = -1
HDR_RE = re.compile(r'^([^\.]*)(?:\.(Flag|\d+))?$')
def parse_hdr(h):
    m = HDR_RE.match(h)
    if not m:
        raise ValueError("Bad header: %s" % h)
    (desc, idx) = m.groups()
    if idx:
        if idx == "Flag":
            idx = FLAG
        else:
            idx = long(idx)
    return (desc, idx)

def val(s):
    try:
        return long(s)
    except:
        try:
            return float(s)
        except:
            return s

def assign(a, idx, v):
    dim = len(a)
    e = idx - dim + 1
    if e > 0:
        a.extend(e * [None])
    a[idx] = v

def vmap(op, *args):
    maxdim = 1
    dim = []
    for a in args:
        try:
            d = len(a)
        except:
            d = 0
        dim.append(d)
        maxdim = max(maxdim, d)
    aa = [ (maxdim * [a] if d == 0 else a) for a,d in zip(args,dim) ]
    return map(op, *aa)

class ResultRow(object):
    """
    Collection of Results
    """
    def __init__(self, hdr, row_text):
        self.coll = dict()
        for h,v in zip(hdr, row_text):
            (desc, idx) = parse_hdr(h)
            v = val(v)
            r = self.coll.get(desc)
            if not r:
                r = Result(desc)
                self.coll[desc] = r
            if idx == FLAG:
                r.flag = v
            elif idx == None:
                # scalar
                r.val = v
            else:
                # vector
                assign(r.val, idx, v)
                r.dim = len(r.val)
        for r in self.coll.itervalues():
            if type(r.val) == list:
                r.val = tuple(r.val)

    def __repr__(self):
        sio = StringIO()
        sio.write('(')
        first = 1
        for k, v in self.coll.iteritems():
            if not first:
                sio.write(', ')
            sio.write(str(v))
            first = 0
        sio.write(')')
        return sio.getvalue()

    def __str__(self):
        return repr(self)

    def __iter__(self):
        for k,v in self.coll.iteritems():
            yield v

    def __getitem__(self, key):
        return self.coll[key]

    def __contains__(self, key):
        return key in self.coll

    def verify(self, prevRow):
        for rv in self:
            if rv.flag == None:
                continue # This is not a derived metric
            if rv.flag == 1:
                continue # Bad derivation
            if not prevRow and (rv.expr.fn_type == FN.DELTA
                                or rv.expr.fn_type == FN.RATE):
                continue # DELTA & RATE needs prevRow
            v = rv.expr.eval(self, prevRow)
            assert(rv.val == v)

    def as_tuple(self, keys = None):
        if not keys:
            keys = self.coll.keys()
        return tuple(self.coll[k].val for k in keys)

class ResultFile(object):
    """
    Collection of ResultRow
    """
    def __init__(self, csv_file):
        f = open(csv_file, "r")
        rdr = csv.reader(f)
        self.hdr = next(rdr)
        self.rows = [ResultRow(self.hdr, r) for r in rdr]

    def __iter__(self):
        return iter(self.rows)

    def verify(self):
        prevRow = None
        for r in self.rows:
            r.verify(prevRow)
            prevRow = r


#############################
#-- LDMS helper functions --#
#############################

def wrap(val):
    if type(val) == list:
        return tuple(val)
    return val

def ldms_set_as_tuple(_set, with_ts = False):
    t = tuple( wrap(_val) for (_mid, _val) in _set.iter_items() )
    if with_ts:
        ts = _set.ts_get()
        ts = ts.sec + ts.usec*1e-6
        t = (ts,) + t
    return t

#############################
#--  derived.conf content --#
#############################
derived_conf = """\
# fmt: schema dname op num_operands operands(csv) scale write_out
# ----------------------------------------------

## rawterm
sch  job_id  RAWTERM  1  job_id  1  1
sch  app_id  RAWTERM  1  app_id  1  1
sch  s0  RAWTERM  1  s0  1  1
sch  s1  RAWTERM  1  s1  1  1
sch  s2  RAWTERM  1  s2  1  1
sch  v0  RAWTERM  1  v0  1  1
sch  v1  RAWTERM  1  v1  1  1
sch  v2  RAWTERM  1  v2  1  1

## raw
sch  "raw(s0)"  RAWTERM  1  s0  1  1
sch  "raw(s1)"  RAWTERM  1  s1  1  1
sch  "raw(s2)"  RAWTERM  1  s2  1  1
sch  "raw(v0)"  RAWTERM  1  v0  1  1
sch  "raw(v1)"  RAWTERM  1  v1  1  1
sch  "raw(v2)"  RAWTERM  1  v2  1  1

## scalar: univariate
sch  "delta(s0)"        DELTA      1  s0  1  1
sch  "rate(s0)"         RATE       1  s0  1  1
sch  "thresh_ge(s0,5)"  THRESH_GE  1  s0  5  1
sch  "thresh_lt(s0,5)"  THRESH_LT  1  s0  5  1
## scalar: bivariate
sch  "sum_n(s1,s0)"   SUM_N   2  s1,s0  1  1
sch  "sub_ab(s1,s0)"  SUB_AB  2  s1,s0  1  1
sch  "mul_ab(s1,s0)"  MUL_AB  2  s1,s0  1  1
sch  "div_ab(s1,s0)"  DIV_AB  2  s1,s0  1  1
## scalar: multivariate
sch  "sum_n(s0,s1,s2)"  SUM_N  3  s0,s1,s2  1  1
sch  "avg_n(s0,s1,s2)"  AVG_N  3  s0,s1,s2  1  1
sch  "min_n(s0,s1,s2)"  MIN_N  3  s0,s1,s2  1  1
sch  "max_n(s0,s1,s2)"  MAX_N  3  s0,s1,s2  1  1

## vector: univariate
sch  "sum(v0)"          SUM        1  v0  1  1
sch  "avg(v0)"          AVG        1  v0  1  1
sch  "min(v0)"          MIN        1  v0  1  1
sch  "max(v0)"          MAX        1  v0  1  1
sch  "delta(v0)"        DELTA      1  v0  1  1
sch  "rate(v0)"         RATE       1  v0  1  1
sch  "thresh_ge(v0,5)"  THRESH_GE  1  v0  5  1
sch  "thresh_lt(v0,5)"  THRESH_LT  1  v0  5  1
## vector: bivariate
sch  "sum_n(v1,v0)"   SUM_N   2  v1,v0  1  1
sch  "sub_ab(v1,v0)"  SUB_AB  2  v1,v0  1  1
sch  "mul_ab(v1,v0)"  MUL_AB  2  v1,v0  1  1
sch  "div_ab(v1,v0)"  DIV_AB  2  v1,v0  1  1
## vector: multivariate
sch  "sum_n(v0,v1,v2)"  SUM_N  3  v0,v1,v2  1  1
sch  "avg_n(v0,v1,v2)"  AVG_N  3  v0,v1,v2  1  1
sch  "min_n(v0,v1,v2)"  MIN_N  3  v0,v1,v2  1  1
sch  "max_n(v0,v1,v2)"  MAX_N  3  v0,v1,v2  1  1

## vector - scalar
sch  "sum_vs(v0,s0)"  SUM_VS  2  v0,s0  1  1
sch  "sub_vs(v0,s0)"  SUB_VS  2  v0,s0  1  1
sch  "sub_sv(s2,v0)"  SUB_SV  2  s2,v0  1  1
sch  "mul_vs(v0,s0)"  MUL_VS  2  v0,s0  1  1
sch  "div_vs(v0,s0)"  DIV_VS  2  v0,s0  1  1
sch  "div_sv(s0,v0)"  DIV_SV  2  s0,v0  1  1
"""

######################
#-- The test class --#
######################
class TestStoreFunctionCSV(unittest.TestCase):
    """Test cases for ldmsd store_function_csv plugin"""
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = DIR + "/smp.log" # for debugging
    AGG_PORT = "11001"
    AGG_LOG = DIR + "/agg.log" # for debugging
    PRDCR = HOSTNAME + ":" + SMP_PORT
    STORE_PATH = DIR + "/csv"
    DERIVED_CONF = DIR + "/derived.conf"

    # LDMSD instances
    smp = None
    agg = None

    @classmethod
    def setUpClass(cls):
        if os.path.exists(cls.STORE_PATH):
            os.remove(cls.STORE_PATH)
        with open(cls.DERIVED_CONF, "w") as f:
            f.write(derived_conf)

        try:
            smpcfg = """
                load name=test plugin=test_sampler
                config name=test component_id=20
                config name=test action=add_schema schema=sch \
                       metrics=s0:data:U64:0,s1:data:U64:1,s2:data:U64:2,\
v0:data:U64_ARRAY:0:3,v1:data:U64_ARRAY:1:3,v2:data:U64_ARRAY:2:3
                config name=test action=add_set schema=sch \
                       instance=test

                smplr_add name=smp_test instance=test interval=1000000 offset=0
                smplr_start name=smp_test
            """
            cls.smp = LDMSD(port = cls.SMP_PORT, cfg = smpcfg,
                            logfile = cls.SMP_LOG)
            cls.smp.run()
            time.sleep(2.0)

            aggcfg = """
                prdcr_add name=smp type=active xprt=%(XPRT)s port=%(SMP_PORT)s \
                          host=localhost interval=1000000
                prdcr_start name=smp

                updtr_add name=upd interval=1000000 offset=500000
                updtr_prdcr_add name=upd regex=.*
                updtr_start name=upd

                load name=csv plugin=store_function_csv
                config name=csv path=%(STORE_PATH)s \
                       derivedconf=%(DERIVED_CONF)s buffer=0

                strgp_add name=test_csv container=csv schema=sch
                strgp_prdcr_add name=test_csv regex=.*
                strgp_start name=test_csv
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
        rf = ResultFile(self.STORE_PATH)
        # Verify the computed results
        rf.verify()
        # Now, verify that the stored raw is good
        names = [s.metric_name_get(k) for k, v in s.iter_items()]
        names = ["#Time"] + names
        csv_data = set( r.as_tuple(names) for r in rf )
        self.assertLessEqual(data, csv_data)


if __name__ == "__main__":
    startup = os.getenv("PYTHONSTARTUP")
    if startup:
        execfile(startup)
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    if not os.path.exists('log'):
        os.mkdir('log')
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = DIR + "/test.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

#!/usr/bin/env python

# Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2016 Sandia Corporation. All rights reserved.
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

import logging
import unittest
import abhttp
import re
import subprocess
import time
import os
from StringIO import StringIO
from datetime import datetime

import util

logger = logging.getLogger(__name__)

CONFIG_PATH = ".test_service.cfg"

class TestConfig(unittest.TestCase):
    CONFIG_STR = """\
    sources:
        bhttpd0: ["http://localhost:18000", "http://localhost:18800"]
        bhttpd1: ["http://localhost:18001", "http://localhost:18801"]
        bhttpd2: ["http://localhost:18002", "http://localhost:18802"]
        bhttpd3: ["http://localhost:18003", "http://localhost:18803"]
    store: ./svc.store
    """
    def setUp(self):
        self.CFG_PATH = CONFIG_PATH
        f = open(self.CFG_PATH, "w")
        f.write(self.CONFIG_STR)
        f.close()

    def test_config_file(self):
        cfg = abhttp.Config.from_yaml_file(self.CFG_PATH)
        logger.info("testing bhttpd section iterator.")
        for (name, addrs) in cfg.sources_iter():
            logger.info("name(%s), addrs: %s", name, addrs)
        logger.info("------------------------------")

    def test_wrong_format(self):
        _str = """\
sources:
    bhttpd0: ["http://localhost", "http://localhost:18800"]
    bhttpd1: ["http://localhost:18001", "http://localhost:18801"]
    bhttpd2: ["http://localhost:18002", "http://localhost:18802"]
    bhttpd3: ["http://localhost:18003", "http://localhost:18803"]
"""
        with self.assertRaises(TypeError):
            cfg = abhttp.Config.from_yaml_stream(_str)

    def tearDown(self):
        pass


class TestService(unittest.TestCase):

    CONFIG_STR = """\
    sources:
        bhttpd0: ["http://localhost:18000", "http://localhost:18800"]
        bhttpd1: ["http://localhost:18001", "http://localhost:18801"]
        bhttpd2: ["bstore://%(cwd)s/tmp/TestService/store.2"]
        bhttpd3: ["bstore://%(cwd)s/tmp/TestService/store.3"]
    store: ./svc.store
    """ % {
        "cwd": os.getcwd()
    }

    @classmethod
    def setUpClass(cls):
        #cls is a class object here
        cls.testENV = util.TestENV(util.Param(
                                TMP_DIR='./tmp/TestService',
                                BHTTPD_MAIN = False,
                                BHTTPD_BAK = True,
                        ))
        cls.svc = abhttp.Service(cfg_stream=StringIO(cls.CONFIG_STR))
        cls.numeric_assign_host()
        cls.numeric_assign_ptn()

    @classmethod
    def numeric_assign_host(cls):
        hosts = [ x for x in cls.svc.uhost_iter() ]
        for (_id, _text) in hosts:
            m = re.match("node([0-9]+)", _text)
            hid = int(m.group(1))
            cls.svc.uhost_assign(hid, _text)

    @classmethod
    def numeric_assign_ptn(cls):
        ptns = [ x for x in cls.svc.uptn_iter() ]
        ptn = re.compile("^.*pattern " +
                "(((Zero|One|Two|Three|Four|Five|Six|Seven|Eight|Nine) *)+):")
        for p in ptns:
            m = re.match(ptn, p.text)
            t = m.group(1)
            n = 0
            for x in t.split(' '):
                n *= 10
                n += util.num[x]
            logger.debug("assigning id: %d, ptn: '%s'" % (n, p.text))
            cls.svc.uptn_assign(n, p.text)
            if re.match("This is pattern Zero: .*", p.text):
                cls.ptn_zero = n
        assert(cls.ptn_zero != None)

    def test_uptn_autoassign(self):
        svc = abhttp.Service(cfg_stream=StringIO(self.CONFIG_STR))
        u0 = {p.text: p for p in svc.uptn_iter()}
        svc.uptn_autoassign()
        u1 = {p.text: p for p in svc.uptn_iter()}
        ids = set( p.ptn_id for p in svc.uptn_iter() )
        for p in svc.uptn_iter():
            self.assertTrue(p.ptn_id != None)
            ids.remove(p.ptn_id)
            del u0[p.text]
        self.assertTrue(len(u0) == 0)
        self.assertTrue(len(ids) == 0)

    def test_uhost(self):
        svc = self.svc
        s = set()
        count = 0
        for h in svc.uhost_iter():
            count += 1
            s.add(h.text)
            logger.debug("uhost - %s: %s", h.host_id, h.text)
        self.assertEqual(count, len(s))

    def test_uptn_update(self):
        param = self.testENV.param
        svc = self.svc
        max_p = None
        for p in svc.uptn_iter():
            if max_p == None or max_p.ptn_id < p.ptn_id:
                max_p = p
        next_id = max_p.ptn_id + 1
        ptn_temp = util.gen_ptn_template(next_id)
        msg_text = ptn_temp % 0
        ts = abhttp.Timestamp(param.TS_BEGIN + param.TS_LEN, 0)
        msg = abhttp.LogMessage(ts, util.host_name(0), msg = msg_text)
        b = self.testENV.balerd[0]
        proc = subprocess.Popen(["./syslog2baler.pl", "-p", str(b.syslog_port)],
                                stdin=subprocess.PIPE)
        proc.stdin.write(str(msg) + "\n")
        proc.stdin.close()
        time.sleep(0.5) # make sure that balerd got the message
        svc.uptn_update()
        xptn = None
        for p in svc.uptn_iter():
            if p.ptn_id == None:
                self.assertTrue(xptn == None)
                xptn = p
        self.assertTrue(xptn != None)
        # xptn is the unassigned pattern.
        self.numeric_assign_ptn()
        ptn = svc.uptn_by_id(next_id)
        self.assertEqual(xptn.text, ptn.text)

    def test_uhost_update(self):
        param = self.testENV.param
        svc = self.svc
        new_host = util.host_name(65536)
        ptn_temp = util.gen_ptn_template(0)
        msg_text = ptn_temp % 0
        ts = abhttp.Timestamp(param.TS_BEGIN + param.TS_LEN, 0)
        msg = abhttp.LogMessage(ts=ts, host=new_host, msg=msg_text)
        b = self.testENV.balerd[0]
        proc = subprocess.Popen(["./syslog2baler.pl", "-p", str(b.syslog_port)],
                                stdin=subprocess.PIPE)
        proc.stdin.write(str(msg) + "\n")
        proc.stdin.close()
        time.sleep(0.5) # make sure that balerd got the message
        svc.uhost_update()
        xhost = None
        for h in svc.uhost_iter():
            if h.host_id != None:
                continue
            xhost = h.text
        self.assertEqual(new_host, xhost)

    @classmethod
    def tearDownClass(cls):
        logger.info("cleaning up the env")
        del cls.testENV
        time.sleep(2)
        logger.info("ENV clean up done.")

# -- TestService -- #


class TestQueryIter(unittest.TestCase):
    CONFIG_STR = """\
    sources:
        bhttpd0: ["http://localhost:18000", "http://localhost:18800"]
        bhttpd1: ["http://localhost:18001", "http://localhost:18801"]
        bhttpd2: ["bstore://%(cwd)s/tmp/TestUMsgQueryIter/store.2"]
        bhttpd3: ["bstore://%(cwd)s/tmp/TestUMsgQueryIter/store.3"]
    store: ./svc.store
    """ % {
        "cwd": os.getcwd()
    }

    @classmethod
    def setUpClass(cls):
        #cls is a class object here
        cls.testENV = util.TestENV(util.Param(
                                TMP_DIR="./tmp/TestUMsgQueryIter",
                                BHTTPD_MAIN = False,
                                BHTTPD_BAK = True,
                        ))
        cls.svc = abhttp.Service(cfg_stream=StringIO(cls.CONFIG_STR))
        hosts = [ x for x in cls.svc.uhost_iter() ]
        for (_id, _text) in hosts:
            m = re.match("node([0-9]+)", _text)
            hid = int(m.group(1))
            cls.svc.uhost_assign(hid, _text)
        ptns = [ x for x in cls.svc.uptn_iter() ]
        ptn = re.compile("^.*pattern " +
                "(((Zero|One|Two|Three|Four|Five|Six|Seven|Eight|Nine) *)+):")
        for p in ptns:
            m = re.match(ptn, p.text)
            t = m.group(1)
            n = 0
            for x in t.split(' '):
                n *= 10
                n += util.num[x]
            logger.debug("assigning id: %d, ptn: '%s'" % (n, p.text))
            cls.svc.uptn_assign(n, p.text)
            if re.match("This is pattern Zero: .*", p.text):
                cls.ptn_zero = n
        assert(cls.ptn_zero != None)
        for p in cls.svc.uptn_iter():
            logger.debug(p)
        pass

    def _check_coll(self, coll, coll2, hid_set, pid_set):
        if not coll:
            return
        for hid in hid_set:
            for pid in pid_set:
                i0 = hid % self.n_daemons
                i1 = pid % self.n_daemons
                if pid and i0 != i1:
                    continue
                k = (hid, pid)
                msg = coll.pop(k)
                msg2 = coll2.pop(k)
        self.assertEqual(0, len(coll))
        self.assertEqual(0, len(coll2))
        pass

    def get_last_ts(self, ts):
        param = self.testENV.param
        last_ts = param.TS_BEGIN + param.TS_LEN - 1
        if not ts or ts>last_ts:
            ts = last_ts
        inc = param.TS_INC
        return (ts/inc)*inc

    def run_query_correct(self, uhost_ids=None, uptn_ids=None,
                            ts0=None, ts1=None):
        # Test correctness of the query.
        itr = abhttp.UMsgQueryIter(self.svc, host_ids=uhost_ids,
                                    ptn_ids=uptn_ids, ts0=ts0, ts1=ts1)

        param = self.testENV.param

        ts_inc      =  self.ts_inc      =  param.TS_INC
        ts_begin    =  self.ts_begin    =  param.TS_BEGIN
        ts_len      =  self.ts_len      =  param.TS_LEN
        node_begin  =  self.node_begin  =  param.NODE_BEGIN
        node_len    =  self.node_len    =  param.NODE_LEN
        n_daemons   =  self.n_daemons   =  param.N_DAEMONS
        n_patterns  =  self.n_patterns  =  param.N_PATTERNS

        if not uhost_ids:
            uhost_ids = "%d-%d" % (node_begin, node_begin + node_len -1)
        if not uptn_ids:
            uptn_ids = "%d-%d" % (0, n_patterns-1)
        if ts0 == None:
            ts0 = ts_begin
        if ts1 == None:
            ts1 = ts_begin + ts_len

        # round-up for ts0
        ts0 = int((ts0 + ts_inc - 1)/ts_inc)*ts_inc
        # truncate for ts1
        ts1 = self.get_last_ts(ts1)
        citr = util.MsgGenIter(uhost_ids, uptn_ids, ts0, ts1+1, param=param)

        hid_set = abhttp.IDSet()
        hid_set.add_smart(uhost_ids)
        pid_set = abhttp.IDSet()
        pid_set.add_smart(uptn_ids)

        # collect messages for each timestamp
        coll = {}
        coll2 = {}
        prev_ts = ts0

        for msg2 in citr:
            msg = itr.next()
            self.assertNotEqual(msg, None)
            if msg.ts.sec != prev_ts:
                self._check_coll(coll, coll2, hid_set, pid_set)
            prev_ts = msg.ts.sec
            hid = util.host_id(msg.host)
            pid = msg.ptn_id
            coll[(hid, pid)] = msg
            hid2 = util.host_id(msg2.host)
            pid2 = msg2.ptn_id
            coll2[(hid2, pid2)] = msg2

        self.assertEqual(prev_ts, ts1)

    def test_query_correct_cond0(self):
        self.run_query_correct(uptn_ids=[0])
        pass

    def test_query_correct_cond1(self):
        self.run_query_correct(uptn_ids=[1,4])
        pass

    def test_query_correct_cond2(self):
        self.run_query_correct(uptn_ids=[1,2])
        pass

    def test_query_correct_cond3(self):
        self.run_query_correct(uhost_ids=[0, 1, 2])
        pass

    def test_query_correct_cond4(self):
        param = self.testENV.param
        ts0 = param.TS_BEGIN + param.TS_INC
        ts1 = ts0 + param.TS_LEN - 10
        self.run_query_correct(ts0=ts0, ts1=ts1)
        pass

    def test_query_correct_cond5(self):
        param = self.testENV.param
        ts0 = param.TS_BEGIN + param.TS_INC
        ts1 = ts0 + param.TS_LEN - 10
        self.run_query_correct(ts0=ts0, ts1=ts1, uptn_ids="0-3", uhost_ids="0-2")

    def run_dir_cond(self, fwd_first=True, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        itr = abhttp.UMsgQueryIter(self.svc, host_ids=host_ids,
                                    ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        if fwd_first:
            _next = itr.next
            _prev = itr.prev
            _a = "-- FWD --"
            _b = "-- BWD --"
        else:
            _next = itr.prev
            _prev = itr.next
            _a = "-- BWD --"
            _b = "-- FWD --"
        stack = []
        i = 0
        logger.debug("")
        logger.debug(_a)
        x = _next()
        while x:
            i += 1
            logger.debug("%s", x)
            stack.append(x)
            x = _next()
            if n and i >= n:
                break;

        logger.debug("------")

        logger.debug("")
        logger.debug(_b)
        while i:
            x = _prev()
            logger.debug("%s", x)
            if not x:
                break;
            y = stack.pop()
            self.assertEqual(str(x), str(y))
            i -= 1
        self.assertEqual(0, len(stack))
        self.assertEqual(0, i)
        del itr
        logger.debug("------")
        pass

    def run_fwd_bwd_cond(self, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        self.run_dir_cond(fwd_first=True, n=n, host_ids=host_ids,
                            ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)

    def run_bwd_fwd_cond(self, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        self.run_dir_cond(fwd_first=False, n=n, host_ids=host_ids,
                            ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)

    def test_fwd_bwd_no_cond(self):
        self.run_fwd_bwd_cond()
        pass

    def test_fwd_bwd_cond0(self):
        self.run_fwd_bwd_cond(n=5)
        pass

    def test_bwd_fwd_no_cond(self):
        self.run_bwd_fwd_cond()
        pass

    def test_bwd_fwd_cond0(self):
        self.run_bwd_fwd_cond(n=5)
        pass

    def test_ptn_ids(self):
        self.run_fwd_bwd_cond(ptn_ids=str(self.ptn_zero))
        self.run_bwd_fwd_cond(ptn_ids=str(self.ptn_zero))
        pass

    def run_test_fwd(self, host_ids=None, ptn_ids=None, ts0=None, ts1=None):
        logger.debug("ptn_ids: %s", ptn_ids)
        for x in self.svc.uptn_iter():
            logger.debug("%s", x)
        itr = abhttp.UMsgQueryIter(self.svc, host_ids=host_ids,
                                    ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        msg = itr.next()
        count = 0
        while msg:
            count += 1
            msg = itr.next()
        logger.debug("count: %s", count)
        pass

    def x_test_fwd_case0(self):
        self.run_test_fwd(ptn_ids=self.ptn_zero)
        pass

    def run_test_bwd(self):
        itr = abhttp.UMsgQueryIter(self.svc)
        msg = itr.prev()
        count = 0
        while msg:
            count += 1
            msg = itr.prev()
        logger.debug("count: %s", count)
        pass

    def run_dir_pos_cond(self, fwd_first=True, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        itr = abhttp.UMsgQueryIter(self.svc, host_ids=host_ids,
                                    ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        itr2 = abhttp.UMsgQueryIter(self.svc, host_ids=host_ids,
                                    ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        if fwd_first:
            _next = itr.next
            _posdir = abhttp.BWD
            _prev = itr2.prev
            _a = "-- FWD --"
            _b = "-- BWD --"
        else:
            _next = itr.prev
            _posdir = abhttp.FWD
            _prev = itr2.next
            _a = "-- BWD --"
            _b = "-- FWD --"
        stack = []
        i = 0
        logger.debug("")
        logger.debug(_a)
        x = _next()
        pos = None
        while x:
            i += 1
            logger.debug("%s", x)
            stack.append(x)
            pos = itr.get_pos()
            x = _next()
            if n and i >= n:
                break;

        logger.debug("------")

        logger.debug("")
        logger.debug(_b)
        logger.debug("pos: %s", pos)

        itr2.set_pos(pos)
        pos2 = itr2.get_pos()
        logger.debug("pos2: %s", pos2)

        self.assertEqual(pos, pos2)

        x = itr2.get_curr_msg()
        y = stack.pop()
        self.assertEqual(str(x), str(y))
        i -= 1

        while i:
            x = _prev()
            logger.debug("%s", x)
            if not x:
                break;
            y = stack.pop()
            self.assertEqual(str(x), str(y))
            i -= 1
        self.assertEqual(0, len(stack))
        self.assertEqual(0, i)
        del itr
        logger.debug("------")
        pass

    def test_dir_pos_no_cond(self):
        self.run_dir_pos_cond(n = 5)

    def test_dir_pos_cond0(self):
        self.run_dir_pos_cond(n =5, ptn_ids=str(self.ptn_zero))


    def run_img_query_correct(self, uhost_ids=None, uptn_ids=None,
                              ts0=None, ts1=None):
        # Test correctness of the image query.
        uitr = abhttp.UImgQueryIter(service=self.svc, img_store="3600-1",
                                    host_ids=uhost_ids, ptn_ids=uptn_ids,
                                    ts0=ts0, ts1=ts1
                                )
        citr = util.MsgGenIter(uhost_ids, uptn_ids, ts0, ts1,
                               param=self.testENV.param)
        pxl_buff = {}
        for msg in citr:
            logger.debug("msg: %s", msg)
            key_sec = int(msg.ts.sec/3600) * 3600
            pxl = abhttp.Pixel(ptn_id=msg.ptn_id,
                               sec=key_sec,
                               comp_id=util.host_id(msg.host),
                               count=0)
            try:
                pxl = pxl_buff[pxl.key]
            except KeyError:
                pxl_buff[pxl.key] = pxl
            pxl.count += 1
        pxls = [p for p in pxl_buff.values()]
        pxls.sort()
        for p in pxls:
            logger.debug("p: %s", p)
        logger.debug("p count: %d", len(pxls))
        for pxl in uitr:
            p = pxl_buff.pop(pxl.key)
            self.assertEqual(p, pxl)
        if len(pxl_buff):
            pxls = [p for p in pxl_buff.values()]
            pxls.sort()
            for p in pxls:
                logger.debug("junk p: %s", p)
        self.assertEqual(len(pxl_buff), 0)

    def test_img_query_cond0(self):
        self.run_img_query_correct()

    def test_img_query_cond1(self):
        self.run_img_query_correct(uptn_ids=0)

    def test_img_query_cond2(self):
        param = self.testENV.param
        ts0 = int(param.TS_BEGIN/3600)*3600
        ts1 = ts0 + 2*3600 - 1
        logger.debug("---")
        logger.debug("ts0: %d", ts0)
        logger.debug("ts1: %d", ts1)
        for sec in xrange(ts0, ts1, param.TS_INC):
            logger.debug("sec: %d", sec)
        logger.debug("---")
        self.run_img_query_correct(ts0=ts0,
                                   ts1=ts1)

    def test_img_query_cond3(self):
        param = self.testENV.param
        ts0 = int(param.TS_BEGIN/3600)*3600
        ts1 = ts0 + 2*3600 - 1
        self.run_img_query_correct(uptn_ids="0", uhost_ids="0",
                                   ts0=ts0,
                                   ts1=ts1)


    @classmethod
    def tearDownClass(cls):
        #self is a class object here
        del cls.svc
        logger.info("cleaning up the env")
        del cls.testENV
        time.sleep(2)
        logger.info("ENV clean up done.")

# -- TestUMsgQueryIter -- #


if __name__ == "__main__":
    LOGFMT = '%(asctime)s %(name)s %(funcName)s %(levelname)s: %(message)s'
    logging.basicConfig(format=LOGFMT)
    logger.setLevel(logging.INFO)
    unittest.main()

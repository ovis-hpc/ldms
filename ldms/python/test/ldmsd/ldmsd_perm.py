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

# This file contains test cases for various ldmsd object permissions

from builtins import str
import logging
import unittest
import threading
import time
import re
import json
import os
from io import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class TestLDMSDPerm(unittest.TestCase):
    LDMSD_UID = os.geteuid()
    LDMSD_GID = os.getegid()
    AUTH = "naive"
    LDMSD_AUTH_OPT = {"uid": LDMSD_UID, "gid": LDMSD_GID}
    BOB_AUTH_OPT = {"uid": "2222", "gid": LDMSD_GID}
    ALICE_AUTH_OPT = {"uid": "3333", "gid": LDMSD_GID}
    XPRT = "sock"
    BOB = "bob"
    ALICE = "alice"
    PRDCRS = [
        {
            "prdcr": "prdcr_"+BOB,
            "port":"10001",
            "auth": AUTH,
            "auth_opt": BOB_AUTH_OPT,
            "perm": "0600",
            "xprt": XPRT,
            "updtr": "updtr_"+BOB,
            "logfile": None
        },
        {
            "prdcr": "prdcr_"+ALICE,
            "port":"10002",
            "auth": AUTH,
            "auth_opt": ALICE_AUTH_OPT,
            "perm": "0600",
            "xprt": XPRT,
            "updtr": "updtr_"+ALICE,
            "logfile": None
        },
    ]
    AGG_PORT = "10000"
    AGG_LOG = None

    # sampler instances
    prdcrs = []
    agg = None

    @classmethod
    def setUpClass(cls):
        # Need 3 ldmsd .. the config objects are for aggregators
        log.info("Setting up TestLDMSDPerm")
        try:
            # samplers (producers)
            for prdcr in cls.PRDCRS:
                smp_cfg = """
                    load name=meminfo
                    config name=meminfo producer=%(prdcr)s \
                           instance=%(prdcr)s/meminfo schema=meminfo
                    start name=meminfo interval=1000000 offset=0
                """ % prdcr
                log.debug("smp_cfg: %s" % smp_cfg)
                ldmsd = LDMSD(port = prdcr["port"], xprt = cls.XPRT,
                              auth = cls.AUTH, auth_opt = cls.LDMSD_AUTH_OPT,
                              cfg = smp_cfg,
                              logfile = prdcr["logfile"])
                log.info("starting %s" % prdcr["prdcr"])
                ldmsd.run()
                cls.prdcrs.append(ldmsd)

            # aggregator
            cls.agg = LDMSD(port = cls.AGG_PORT, xprt = cls.XPRT,
                            auth = cls.AUTH, auth_opt = cls.LDMSD_AUTH_OPT,
                            logfile = cls.AGG_LOG)
            log.info("starting aggregator")
            cls.agg.run()
            time.sleep(2)

            # need to config separately so that prdcr,updtr pairs are owned by
            # different users.
            log.info("configuring aggregator")
            for prdcr in cls.PRDCRS:
                log.info("....adding %(prdcr)s" % prdcr)
                agg_cfg = """\
                prdcr_add name=%(prdcr)s xprt=%(xprt)s host=localhost \
                          port=%(port)s type=active interval=1000000 \
                          perm=0600
                prdcr_start name=%(prdcr)s
                updtr_add name=%(updtr)s interval=1000000 offset=500000 \
                          perm=0600
                updtr_prdcr_add name=%(updtr)s regex=%(prdcr)s
                updtr_start name=%(updtr)s
                """ % prdcr
                log.debug("agg_cfg: %s" % agg_cfg)
                ctrl = ldmsdInbandConfig(host = "localhost",
                                        port = cls.AGG_PORT,
                                        xprt = prdcr["xprt"],
                                        auth = prdcr["auth"],
                                        auth_opt = prdcr["auth_opt"])
                for cmd in agg_cfg.splitlines():
                    cmd = cmd.strip()
                    if not cmd:
                        continue
                    log.debug("cmd: %s" % cmd)
                    req = LDMSD_Request.from_str(cmd)
                    req.send(ctrl)
                    resp = req.receive(ctrl)
                    errcode = resp["errcode"]
                    if errcode:
                        raise RuntimeError("LDMSD Ctrl errcode: %d" % errcode)
                ctrl.close()
            time.sleep(2)
            # Verify that the agg is working as configured
            log.info("verifying aggregator")
            xprt = ldms.Xprt(cls.XPRT, cls.AUTH, cls.LDMSD_AUTH_OPT)
            xprt.connect("localhost", cls.AGG_PORT)
            _dir = xprt.dir()
            _dir = [ d.name for d in _dir ]
            log.debug("dirs: %s" % str(_dir))
            xprt.close()
            _edirs = [ p["prdcr"]+"/meminfo" for p in cls.PRDCRS ]
            if set(_dir) != set(_edirs):
                import pdb
                pdb.set_trace()
                raise RuntimeError("Bad set ...")
        except:
            del cls.agg
            del cls.prdcrs
            raise
        log.info("TestLDMSDPerm set up done")

    @classmethod
    def tearDownClass(cls):
        del cls.prdcrs
        del cls.agg

    def setUp(self):
        log.debug("Testing: %s" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def _ldmsd_ctrl(self, cmd, uid=LDMSD_UID, gid=LDMSD_GID):
        cfg = ldmsdInbandConfig(host="localhost", port=self.AGG_PORT,
                                xprt=self.XPRT, auth=self.AUTH,
                                auth_opt={"uid": str(uid), "gid": str(gid)})
        req = LDMSD_Request.from_str(cmd)
        req.send(cfg)
        resp = req.receive(cfg)
        cfg.close()
        return resp

    def test_prdcr_status_other(self):
        resp = self._ldmsd_ctrl("prdcr_status", uid="5555", gid="5555")
        self.assertEqual(0, resp["errcode"])
        msg = json.loads(resp["msg"])
        # msg is an array of statuses of producers
        names0 = set([x["name"] for x in msg])
        names1 = set([x["prdcr"] for x in self.PRDCRS])
        self.assertEqual(names0, names1)
        # We don't care about the status of the prdcr

    def test_prdcr_status_self(self):
        resp = self._ldmsd_ctrl("prdcr_status")
        self.assertEqual(0, resp["errcode"])
        msg = json.loads(resp["msg"])
        # msg is an array of statuses of producers
        names0 = set([x["name"] for x in msg])
        names1 = set([x["prdcr"] for x in self.PRDCRS])
        self.assertEqual(names0, names1)
        # We don't care about the status of the prdcr

    def test_prdcr_stop_other(self):
        bob_prdcr = "prdcr_" + self.BOB
        resp = self._ldmsd_ctrl("prdcr_stop name="+bob_prdcr,
                                uid="5555", gid="5555")
        # Expect error response
        self.assertNotEqual(0, resp["errcode"])
        self.assertEqual("Operation not permitted.", resp["msg"])
        log.debug("resp: %s" % str(resp))

    def test_prdcr_bob_stop_by_alice(self):
        bob_prdcr = "prdcr_" + self.BOB
        alice_uid = self.ALICE_AUTH_OPT["uid"]
        alice_gid = self.ALICE_AUTH_OPT["gid"]
        resp = self._ldmsd_ctrl("prdcr_stop name="+bob_prdcr,
                                uid=alice_uid, gid=alice_gid)
        # Expect error response
        self.assertNotEqual(0, resp["errcode"])
        self.assertEqual("Permission denied.", resp["msg"])
        log.debug("resp: %s" % str(resp))

    def test_prdcr_bob_del_by_alice(self):
        bob_prdcr = "prdcr_" + self.BOB
        alice_uid = self.ALICE_AUTH_OPT["uid"]
        alice_gid = self.ALICE_AUTH_OPT["gid"]
        resp = self._ldmsd_ctrl("prdcr_del name="+bob_prdcr,
                                uid=alice_uid, gid=alice_gid)
        # Expect error response
        self.assertNotEqual(0, resp["errcode"])
        self.assertEqual("Permission denied.", resp["msg"])
        log.debug("resp: %s" % str(resp))

    def test_updtr_bob_stop_by_alice(self):
        bob_updtr = "updtr_" + self.BOB
        alice_uid = self.ALICE_AUTH_OPT["uid"]
        alice_gid = self.ALICE_AUTH_OPT["gid"]
        resp = self._ldmsd_ctrl("updtr_stop name="+bob_updtr,
                                uid=alice_uid, gid=alice_gid)
        # Expect error response
        self.assertNotEqual(0, resp["errcode"])
        self.assertEqual("Permission denied.", resp["msg"])
        log.debug("resp: %s" % str(resp))

    def test_updtr_bob_del_by_alice(self):
        bob_updtr = "updtr_" + self.BOB
        alice_uid = self.ALICE_AUTH_OPT["uid"]
        alice_gid = self.ALICE_AUTH_OPT["gid"]
        resp = self._ldmsd_ctrl("updtr_del name="+bob_updtr,
                                uid=alice_uid, gid=alice_gid)
        # Expect error response
        self.assertNotEqual(0, resp["errcode"])
        self.assertEqual("Permission denied.", resp["msg"])
        log.debug("resp: %s" % str(resp))

    def test_prdcr_bob_self(self):
        # stop
        prdcr = "prdcr_" + self.BOB
        uid = self.BOB_AUTH_OPT["uid"]
        gid = self.BOB_AUTH_OPT["gid"]
        resp = self._ldmsd_ctrl("prdcr_stop name="+prdcr, uid=uid, gid=gid)
        self.assertEqual(0, resp["errcode"])
        # status
        resp = self._ldmsd_ctrl("prdcr_status", uid=uid, gid=gid)
        msg = json.loads(resp["msg"])
        (pstat,) = filter(lambda x: x["name"] == prdcr, msg)
        self.assertEqual("STOPPED", pstat["state"])
        # start
        resp = self._ldmsd_ctrl("prdcr_start name="+prdcr, uid=uid, gid=gid)
        self.assertEqual(0, resp["errcode"])
        # wait a bit
        time.sleep(2)
        # status
        resp = self._ldmsd_ctrl("prdcr_status", uid=uid, gid=gid)
        msg = json.loads(resp["msg"])
        (pstat,) = filter(lambda x: x["name"] == prdcr, msg)
        self.assertEqual("CONNECTED", pstat["state"])


if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_perm.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

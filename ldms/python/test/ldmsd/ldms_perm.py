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

import logging
import unittest
import threading
import time
import re
import os

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)

class Debug(object): pass

DEBUG = Debug()

class TestLDMSPerm(unittest.TestCase):
    UID = "1234"
    GID = "1234"
    XPRT = "sock"
    PORT = "10001"
    AUTH = "naive"
    AUTH_OPT = {
        "uid": UID,
        "gid": GID,
    }
    SETS =  ["meminfo", "vmstat", "array_example"]
    PERMS = ["0600",    "0660",   "0666"]

    @classmethod
    def setUpClass(cls):
        log.info("Setting up TestLDMSAuthNaive")
        ldms.ldms_init(512*1024*1024) # 512MB should suffice
        cls.ldmsd = LDMSD(port=cls.PORT, auth=cls.AUTH,
                          auth_opt = cls.AUTH_OPT)
        log.info("starting ldmsd")
        cls.ldmsd.run()
        cls.cfg = ldmsdInbandConfig(host = "localhost", port = cls.PORT,
                                    xprt = cls.XPRT, auth = cls.AUTH,
                                    auth_opt = cls.AUTH_OPT)
        # NOTE: cls.cfg automatically create an LDMS xprt and connect to the
        #       target ldmsd.
        cmds = []
        for _set, _perm in zip(cls.SETS, cls.PERMS):
            cmds.append("load name=%s" % _set)
            cmds.append("config name=%(set)s producer=test1 instance=%(set)s \
                         schema=%(set)s component_id=1 \
                         uid=%(uid)s gid=%(gid)s perm=%(perm)s" % {
                             "set":   _set,
                             "uid":   cls.UID,
                             "gid":   cls.GID,
                             "perm":  _perm,
                         })
            cmds.append("start name=%s interval=1000000" % _set)
        log.info("configuring ldmsd over LDMS xprt")
        for cmd in cmds:
            req = LDMSD_Request.from_str(cmd)
            req.send(cls.cfg)
            resp = req.receive(cls.cfg)
            errcode = resp["errcode"] if "errcode" in resp else 0
            if errcode:
                raise RuntimeError("LDMSD request errcode: %d" % errcode)
        time.sleep(1)
        log.info("TestLDMSAuthNaive setup complete")

    @classmethod
    def tearDownClass(cls):
        log.info("Tearing down TestLDMSAuthNaive")
        cls.cfg.close()
        del cls.ldmsd

    def test_wrong_auth(self):
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, "none", None)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        self.assertNotEqual(rc, 0)

    def test_dir_owner(self):
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, self.AUTH_OPT)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        if rc:
            raise RuntimeError("LDMS connect failed: %d" % rc)
        _dir = ldms.LDMS_xprt_dir(xprt)
        self.assertEqual(set(_dir), set(self.SETS))
        ldms.ldms_xprt_close(xprt)

    def test_dir_group(self):
        auth_opt = {"uid": "5555", "gid": self.GID}
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, auth_opt)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        if rc:
            raise RuntimeError("LDMS connect failed: %d" % rc)
        r = re.compile("0.6.")
        _sets = [_s for _s, _p in zip(self.SETS, self.PERMS) if r.match(_p)]
        _dir = ldms.LDMS_xprt_dir(xprt)
        self.assertEqual(set(_dir), set(_sets))
        ldms.ldms_xprt_close(xprt)

    def test_dir_other(self):
        auth_opt = {"uid": "5555", "gid": "5555"}
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, auth_opt)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        if rc:
            raise RuntimeError("LDMS connect failed: %d" % rc)
        r = re.compile("0..6")
        _sets = [_s for _s, _p in zip(self.SETS, self.PERMS) if r.match(_p)]
        _dir = ldms.LDMS_xprt_dir(xprt)
        self.assertEqual(set(_dir), set(_sets))
        ldms.ldms_xprt_close(xprt)

    def test_lookup_owner(self):
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, self.AUTH_OPT)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        if rc:
            raise RuntimeError("LDMS connect failed: %d" % rc)
        for _name, _perm in zip(self.SETS, self.PERMS):
            _set = ldms.LDMS_xprt_lookup(xprt, _name,
                                         ldms.LDMS_LOOKUP_BY_INSTANCE)
            self.assertIsNotNone(_set)
        ldms.ldms_xprt_close(xprt)

    def test_lookup_group(self):
        auth_opt = {"uid": "5555", "gid": self.AUTH_OPT["gid"]}
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, auth_opt)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        if rc:
            raise RuntimeError("LDMS connect failed: %d" % rc)
        for _name, _perm in zip(self.SETS, self.PERMS):
            _set = ldms.LDMS_xprt_lookup(xprt, _name,
                                         ldms.LDMS_LOOKUP_BY_INSTANCE)
            DEBUG.name = _name
            DEBUG.perm = _perm
            DEBUG.set = _set
            if _perm[2] != "0":
                self.assertIsNotNone(_set)
            else:
                self.assertIsNone(_set)
        ldms.ldms_xprt_close(xprt)

    def test_lookup_other(self):
        auth_opt = {"uid": "5555", "gid": "5555"}
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, auth_opt)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        if rc:
            raise RuntimeError("LDMS connect failed: %d" % rc)
        for _name, _perm in zip(self.SETS, self.PERMS):
            _set = ldms.LDMS_xprt_lookup(xprt, _name,
                                         ldms.LDMS_LOOKUP_BY_INSTANCE)
            DEBUG.name = _name
            DEBUG.perm = _perm
            DEBUG.set = _set
            if _perm[3] != "0":
                self.assertIsNotNone(_set)
            else:
                self.assertIsNone(_set)
        ldms.ldms_xprt_close(xprt)


if __name__ == "__main__":
    start = os.getenv("PYTHONSTARTUP")
    if start:
        execfile(start)
    logging.basicConfig(
            format = "%(asctime)s.%(msecs)d %(levelname)s: %(name)s - %(message)s",
            datefmt = "%F %T",
            level = logging.DEBUG
    )
    log = logging.getLogger(__name__)
    # unittest.TestLoader.testMethodPrefix = "test_lookup_group"
    unittest.main(failfast = True, verbosity = 2)

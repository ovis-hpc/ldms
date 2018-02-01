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

# Test LDMSD with ldms_auth_ovis library for legacy ovis_auth support

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

# log = logging.getLogger(__name__)

log = None

class Debug(object): pass

DEBUG = Debug()

class TestLDMSAuthOvis(unittest.TestCase):
    XPRT = "sock"
    PORT = "10001"
    CONF = "ldmsauth.conf"
    OTHER_CONF = "ldmsauth.other.conf"
    AUTH = "ovis"
    AUTH_OPT = {"conf": os.path.abspath(CONF)}
    OTHER_AUTH_OPT = {"conf": os.path.abspath(OTHER_CONF)}

    @classmethod
    def setUpClass(cls):
        log.info("--- Setting up TestLDMSAuthOvis ---")
        ldms.ldms_init(512*1024*1024) # 512MB should suffice

        # auth conf
        conf_text = "secretword=this_is_a_secret"
        f = open(cls.CONF, "w")
        f.write(conf_text)
        f.close()
        os.chmod(cls.CONF, 0600)

        # other conf
        conf_text = "secretword=this_is_another_secret"
        f = open(cls.OTHER_CONF, "w")
        f.write(conf_text)
        f.close()
        os.chmod(cls.OTHER_CONF, 0600)

        # ldmsd sampler conf
        cfg = """\
        load name=meminfo
        config name=meminfo producer=smp instance=smp/meminfo \
                            schema=meminfo component_id=1 \
                            uid=0 gid=0 perm=0600
        start name=meminfo interval=1000000
        """

        cls.ldmsd = LDMSD(port=cls.PORT, cfg = cfg,
                          auth=cls.AUTH, auth_opt = cls.AUTH_OPT)
        log.info("starting ldmsd")
        cls.ldmsd.run()
        time.sleep(1)
        log.info("--- Done setting up TestLDMSAuthOvis ---")

    @classmethod
    def tearDownClass(cls):
        log.info("--- Tearing down TestLDMSAuthOvis ---")
        del cls.ldmsd

    def test_wrong_auth(self):
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, "none", None)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        self.assertNotEqual(rc, 0)

    def test_wrong_secret(self):
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, self.OTHER_AUTH_OPT)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        self.assertNotEqual(rc, 0)

    def test_dir_owner(self):
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, self.AUTH_OPT)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        self.assertEqual(rc, 0)
        _dir = ldms.LDMS_xprt_dir(xprt)
        self.assertEqual(_dir, ["smp/meminfo"])

    def test_ls_owner(self):
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, self.AUTH_OPT)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        self.assertEqual(rc, 0)
        _set = ldms.LDMS_xprt_lookup(xprt, "smp/meminfo", 0)
        self.assertIsNotNone(_set)

    def test_env(self):
        os.putenv("LDMS_AUTH_FILE", self.AUTH_OPT["conf"])
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, None)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        self.assertEqual(rc, 0)
        _dir = ldms.LDMS_xprt_dir(xprt)
        self.assertEqual(_dir, ["smp/meminfo"])

    def test_env_wrong_secret(self):
        os.putenv("LDMS_AUTH_FILE", self.OTHER_AUTH_OPT["conf"])
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH, None)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.PORT)
        self.assertNotEqual(rc, 0)


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
            filename = "ldmsd_auth_ovis.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

#!/usr/bin/env python

# Copyright (c) 2018 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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

# This file contains test cases for ldmsd configuration files

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
import sys
from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

log = logging.getLogger(__name__)
HOSTNAME = socket.gethostname()
DIR = "test_sampler_core"

ldms.ldms_init(512*1024*1024) # 512MB should suffice

def ldms_set_str(_set):
    sio = StringIO()
    print >>sio, _set.instance_name_get()
    for (_mid, _val) in _set.iter_items():
        print >>sio, "  - %s: %s" % (_set.metric_name_get(_mid), str(_val))
    return sio.getvalue()

def adiff(a):
    return [ a[i] - a[i-1] for i in range(1, len(a)) ]

def ldms_set_as_dict(_set):
    return { _set.metric_name_get(_mid): _val \
                                for (_mid, _val) in _set.iter_items() }

class TestSamplerCore(unittest.TestCase):
    """Test cases focusing on ldmsd sampler core configuration"""
    LDMSD_UID = "1111"
    LDMSD_GID = "1111"
    AUTH = "naive"
    LDMSD_AUTH_OPT = {"uid": LDMSD_UID, "gid": LDMSD_GID}
    XPRT = "sock"
    SMP_PORT = "10001"
    SMP_LOG = None # for debuggin
    PRDCR = HOSTNAME + ":" + SMP_PORT

    # LDMSD instances
    smp = None

    @classmethod
    def setUpClass(cls):
        pass

    @classmethod
    def tearDownClass(cls):
        pass

    def setUp(self):
        log.debug("---- %s ----" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def __verify(self, *args, **kwargs):
        try:
            self._verify(*args, **kwargs)
        except:
            if sys.flags.interactive:
                raw_input("\nException caught ... Press ENTER to continue")
            raise
    def _verify(self, xprt, job=None, clk=None, component_id=0,
                       job_id="job_id", app_id="app_id",
                       job_start="job_start", job_end="job_end"):
        set_names = [s for s in [job, clk] if s]
        dir_resp = ldms.LDMS_xprt_dir(xprt)
        self.assertEqual(set(dir_resp), set(set_names))
        if not clk:
            return
        # snapshots of jobset and clkset
        if job:
            jsnap = []
        csnap = []
        if job:
            jobset = ldms.LDMS_xprt_lookup(xprt, job, 0)
        clkset = ldms.LDMS_xprt_lookup(xprt, clk, 0)

        for i in range(0, 3):
            if i:
                time.sleep(1) # skip the 1st sleep
            clkset.update() # blocking-update
            if job:
                jobset.update() # blocking-update
                jsnap.append(ldms_set_as_dict(jobset))
            csnap.append(ldms_set_as_dict(clkset))

        # verify component_id
        comp_id_set = set( c["component_id"] for c in csnap )
        self.assertEqual(set([component_id]), comp_id_set)

        # verify clk["tv_sec"] updates
        ts_set = set( c["tv_sec"] for c in csnap )
        self.assertEqual(2, len(ts_set))
        ts_list = list(ts_set)
        ts_list.sort()
        self.assertEqual(2, ts_list[1] - ts_list[0])

        # check job update (if applicable)
        if not job:
            return
        for c, j in zip(csnap, jsnap):
            self.assertNotEqual(0, c["app_id"])
            self.assertNotEqual(0, c["job_id"])
            self.assertLessEqual(abs(c["job_id"] - j[job_id]), 1)
            self.assertLessEqual(abs(c["app_id"] - j[app_id]), 1)
        job_ids = list(set( c["job_id"] for c in csnap ))
        job_ids.sort()
        d = adiff(job_ids)
        self.assertGreater(len(d), 0)
        self.assertEqual(d, [1 for i in range(0,len(d))])
        app_ids = list(set( c["app_id"] for c in csnap ))
        app_ids.sort()
        d = adiff(app_ids)
        self.assertGreater(len(d), 0)
        self.assertEqual(d, [1 for i in range(0,len(d))])

    def test_00_default(self):
        """Test default options."""
        cfg = """
            load name=clk plugin=clock
            config name=clk

            smplr_add name=smp_clk instance=clk interval=2000000
            smplr_start name=smp_clk
        """
        daemon = LDMSD(port = self.SMP_PORT, auth = self.AUTH,
                       auth_opt = self.LDMSD_AUTH_OPT,
                       cfg = cfg)
        daemon.run()
        time.sleep(2.0)
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH,
                                            self.LDMSD_AUTH_OPT)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.SMP_PORT)
        assert(rc == 0)
        self.__verify(xprt, clk = self.PRDCR + "/clk")
        daemon.term()

    def test_01_default_with_job(self):
        """Test default options with jobinfo set"""
        cfg = """
            load name=jobinfo plugin=faux_job
            config name=jobinfo
            smplr_add name=smp_job instance=jobinfo interval=2000000
            smplr_start name=smp_job

            load name=clk plugin=clock
            config name=clk
            smplr_add name=smp_clk instance=clk interval=2000000
            smplr_start name=smp_clk
        """ % {
            "host": HOSTNAME,
            "port": self.SMP_PORT,
        }
        daemon = LDMSD(port = self.SMP_PORT, auth = self.AUTH,
                       auth_opt = self.LDMSD_AUTH_OPT,
                       cfg = cfg)
        daemon.run()
        time.sleep(2.0)
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH,
                                            self.LDMSD_AUTH_OPT)
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.SMP_PORT)
        assert(rc == 0)
        self.__verify(xprt, job = self.PRDCR + "/jobinfo",
                            clk = self.PRDCR + "/clk")
        daemon.term()

    def test_02_customized_options(self):
        """Test customized options"""
        pass
        cfg = """
            load name=job plugin=faux_job_alt
            config name=job instance=myjob
            smplr_add name=smp_job instance=job interval=2000000 offset=0
            smplr_start name=smp_job

            load name=clk plugin=clock
            config name=clk producer=myprdcr component_id=20 instance=myclk \
                   uid=2222 \
                   gid=3333 \
                   perm=0660 \
                   job_set=myjob \
                   job_id=alt_job_id \
                   app_id=alt_app_id \
                   job_start=alt_job_start \
                   job_end=alt_job_end
            smplr_add name=smp_clk instance=clk interval=2000000 offset=0
            smplr_start name=smp_clk
        """ % {
            "host": HOSTNAME,
            "port": self.SMP_PORT,
        }
        daemon = LDMSD(port = self.SMP_PORT, auth = self.AUTH,
                       auth_opt = self.LDMSD_AUTH_OPT,
                       cfg = cfg)
        log.info("")
        log.info("Starting ldmsd")
        daemon.run()
        time.sleep(4.0)

        # verify with uid/gid 2222
        log.info("Verifying with uid/gid 2222")
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH,
                                            {"uid": "2222", "gid": "2222"})
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.SMP_PORT)
        assert(rc == 0)
        self.__verify(xprt, job="myjob", clk="myclk", component_id=20,
                            job_id="alt_job_id", app_id="alt_app_id",
                            job_start="alt_job_start", job_end="alt_job_end")

        # verify with uid/gid 3333
        log.info("Verifying with uid/gid 3333")
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH,
                                            {"uid": "3333", "gid": "3333"})
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.SMP_PORT)
        assert(rc == 0)
        self.__verify(xprt, job="myjob", clk="myclk", component_id=20,
                            job_id="alt_job_id", app_id="alt_app_id",
                            job_start="alt_job_start", job_end="alt_job_end")

        log.info("Verifying with uid/gid 4444")
        xprt = ldms.LDMS_xprt_new_with_auth(self.XPRT, self.AUTH,
                                            {"uid": "4444", "gid": "4444"})
        rc = ldms.LDMS_xprt_connect_by_name(xprt, "localhost", self.SMP_PORT)
        assert(rc == 0)
        # shouldn't see 'myclk'
        self.__verify(xprt, job="myjob", clk=None, component_id=20,
                            job_id="alt_job_id", app_id="alt_app_id",
                            job_start="alt_job_start", job_end="alt_job_end")

        # terminate ldmsd
        daemon.term()


if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    if not os.path.exists(DIR):
        os.mkdir(DIR)
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = DIR+"/test_sampler_core.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

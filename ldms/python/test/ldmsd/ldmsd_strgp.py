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

# This file contains test cases for ldmsd configuration files

import re
import os
import sys
import time
import shutil
import logging
import unittest
from StringIO import StringIO

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

from sosdb import Sos

log = logging.getLogger(__name__)
DIR = "ldmsd_strgp"
ldms.ldms_init(16*1024*1024)

class TestLDMSDStrgp(unittest.TestCase):
    """Test cases focusing on strgp"""
    XPRT = "sock"
    SMP_PORT_BASE = 10000
    AGG_PORT_BASE = 11000
    SMP_NUM = 4

    # LDMSD instances
    smp = dict()
    agg = None

    @classmethod
    def setUpClass(cls):
        # 1 sampler, 1 aggregator
        log.info("Setting up " + cls.__name__)
        shutil.rmtree(DIR + "/sos", ignore_errors = True)
        try:
            # samplers (producers)
            for i in range(0, cls.SMP_NUM):
                smp_cfg = """
                    load name=meminfo
                    config name=meminfo producer=smp \
                           instance=smp%(id)d/meminfo schema=meminfo \
                           component_id=%(id)d
                    start name=meminfo interval=1000000 offset=0
                """ % {
                    "id": i,
                }
                log.debug("smp_cfg: %s" % smp_cfg)
                logfile = DIR + ('/%d.log' % i)
                port = cls.SMP_PORT_BASE + i
                cls.smp[i] = LDMSD(port = str(port), xprt = cls.XPRT,
                                   cfg = smp_cfg, logfile = logfile)
                log.info("starting sampler %d" % i)
                cls.smp[i].run()
                time.sleep(1)
        except:
            cls.tearDownClass()
            raise
        try:
            sio = StringIO() # holding cfg for agg
            for i in range(0, cls.SMP_NUM):
                prdcr_cfg = """
                    prdcr_add name=%(prdcr)s xprt=%(xprt)s host=localhost \
                              port=%(port)d type=active interval=1000000
                    prdcr_start name=%(prdcr)s
                """ % {
                    "prdcr": "smp%d" % i,
                    "xprt": cls.XPRT,
                    "port": cls.SMP_PORT_BASE + i,
                }
                sio.write(prdcr_cfg)
            cfg = """
                updtr_add name=all interval=1000000 offset=500000
                updtr_prdcr_add name=all regex=.*
                updtr_start name=all

                load name=store_sos
                config name=store_sos path=%(path)s

                strgp_add name=all_but_0 plugin=store_sos container=cont \
                          schema=meminfo
                strgp_prdcr_add name=all_but_0 regex=smp[^0]+
                strgp_start name=all_but_0
            """ % {
                "path": DIR + "/sos"
            }
            sio.write(cfg)
            logfile = DIR + "/agg.log"
            cfg = sio.getvalue()
            cls.agg = LDMSD(port = str(cls.AGG_PORT_BASE), xprt = cls.XPRT,
                            cfg = cfg, logfile = logfile,
                            #gdb_port = 21000,
                            )
            cls.agg.run()
            time.sleep(2)
        except:
            cls.tearDownClass()
            raise
        log.info(cls.__name__ + " set up done")

    @classmethod
    def tearDownClass(cls):
        del cls.smp
        del cls.agg

    def setUp(self):
        log.debug("---- %s ----" % self._testMethodName)

    def tearDown(self):
        log.debug("----------------------------")

    def _data(self):
        db = Sos.Container()
        db.open(DIR + "/sos/cont")
        sch = db.schema_by_name("meminfo")
        attr = sch.attr_by_name("timestamp")
        itr = attr.attr_iter()
        ret = itr.begin()
        while ret:
            obj = itr.item()
            yield obj
            ret = itr.next()
        del itr
        del attr
        del sch
        del db

    def test_000(self):
        """Verify that the agg collects from both smp0 and smp1"""
        x = ldms.LDMS_xprt_new(self.XPRT)
        rc = ldms.LDMS_xprt_connect_by_name(x, "localhost",
                                            str(self.AGG_PORT_BASE))
        self.assertEqual(rc, 0)
        dirs = ldms.LDMS_xprt_dir(x)
        self.assertEqual(
                set(dirs),
                set(["smp%d/meminfo" % i for i in range(0, self.SMP_NUM) ])
            )
        sets = { d: ldms.LDMS_xprt_lookup(x, d, 0) for d in dirs }
        for k, s in sets.iteritems():
            s.update()
            grp = re.match(r"smp(\d+)/meminfo", k).groups()
            comp_id = int(grp[0])
            self.assertEqual(s['component_id'], comp_id)
            ts = s.ts_get()
            self.assertGreater(ts.sec, 0)
        pass

    def test_001(self):
        """Verify that we don't have data from "smp0" """
        count = 0
        while count < 2*self.SMP_NUM:
            time.sleep(1)
            count = 0
            num = [0 for x in range(0, self.SMP_NUM)]
            for o in self._data():
                count += 1
                num[o['component_id']] += 1
        # expect 0 in the first num, and non-zero for others
        exp = [ bool(x) for x in range(0, self.SMP_NUM) ]
        self.assertEqual(map(bool, num), exp)


if __name__ == "__main__":
    if not os.path.isdir(DIR):
        os.makedirs(DIR)
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_strgp/test.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

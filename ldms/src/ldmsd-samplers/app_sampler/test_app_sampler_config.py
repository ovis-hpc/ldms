#!/usr/bin/env python

# Copyright (c) 2020 National Technology & Engineering Solutions
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

# This script tests for data correctness of app_sampler plugin when configured
# with app_sampler cfg_file.

import os
import re
import pdb
import sys
import time
import json
import socket
import logging
import unittest
import subprocess as sp

from collections import namedtuple

from ldmsd.chroot import D, LDMSData, LDMSChrootTest, try_sudo, xvars, Src, \
                         SrcData

from test_app_sampler import array_sum, PIDSrcData, AppSamplerTest

HOSTNAME = socket.gethostname()
LDMSD_PREFIX = HOSTNAME + ":" + str(LDMSChrootTest.PORT)
DIR = "test_app_sampler_config" # a work directory for this test, so that
                                # everything is in one place.
JOB_ID = 5

if not os.path.exists(DIR):
    os.mkdir(DIR)

METRICS = set([ "cmdline", "cmdline_len", "io_write_b", "oom_score", "stat_state",
            "status_threads", "wchan" ])

pids = [ PIDSrcData(p, JOB_ID) for p in [10, 11] ]

def getSrcData(gen):
    # down-select the metrics
    ldms_data = [ pid.getLDMSData(gen) for pid in pids ]
    _M = set(METRICS)
    _M.update(["component_id", "app_id", "job_id"])
    for d in ldms_data:
        d.metrics = frozenset(filter(lambda x: x[0] in _M, d.metrics))
    return SrcData(
                reduce(lambda a,b: a+b, [pid.getSrc(gen) for pid in pids]),
                ldms_data
            )

src_data = [
    # 1st sample
    getSrcData(0),
    # 2nd sample
    getSrcData(1),
]

class AppSamplerConfigTest(AppSamplerTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"
    AUTO_JOB = False
    STREAM = "haha"

    @classmethod
    def getPluginName(cls):
        return "app_sampler"

    @classmethod
    def getSrcData(cls, tidx):
        return src_data[tidx]

    @classmethod
    def ldmsDirLookup(cls):
        # override to allow empty dir results
        cls.d = cls.x.listDir()
        cls.s = list()
        for name in cls.d:
            _s = cls.x.lookupSet(name, 0)
            cls.s.append(_s)

    @classmethod
    def getLdmsConfig(cls):
        # prepare the JSON config file
        f = open("{}/app_sampler.json".format(cls.CHROOT_DIR), "w")
        data = {
            "stream": cls.STREAM,
            "metrics": list(METRICS),
        }
        f.write(json.dumps(data, indent=4))
        f.close()
        # then return the super.config
        return super(AppSamplerConfigTest, cls).getLdmsConfig()

    @classmethod
    def getPluginParams(cls):
        return { "cfg_file": "/app_sampler.json" }

    @classmethod
    def _cleanup(cls):
        if os.path.exists(cls.CHROOT_DIR + "/app_sampler.json"):
            os.remove(cls.CHROOT_DIR + "/app_sampler.json")
        super(AppSamplerConfigTest, cls)._cleanup()


if __name__ == "__main__":
    try_sudo()
    pystart = os.getenv("PYTHONSTARTUP")
    if pystart:
        execfile(pystart)
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = DIR + "/test_app_sampler.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    suite = unittest.TestSuite()
    tests = filter( lambda x: x.startswith('test_'), dir(AppSamplerConfigTest) )
    tests.sort()
    tests = [ AppSamplerConfigTest(t) for t in tests ]
    suite.addTests(tests)
    DEBUG = False
    if DEBUG:
        suite.debug()
    else:
        unittest.TextTestRunner(verbosity=2, failfast=True).run(suite)

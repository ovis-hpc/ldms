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

import os
import re
import pdb
import sys
import time
import socket
import shutil
import logging
import unittest

from collections import namedtuple

from ldmsd.chroot import D, LDMSData, LDMSChrootTest, try_sudo, xvars, \
                         Src, SrcData

HOSTNAME = socket.gethostname()
LDMSD_PREFIX = HOSTNAME + ":" + str(LDMSChrootTest.PORT)
INST_NAME = LDMSD_PREFIX + "/test"
SCHEMA_NAME = "jobinfo"
DIR = "test_jobinfo" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src_data = [
    # 1st SrcData() record for initialization + 1st ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src("/var/run/ldms_jobinfo.data",
                    "JOB_APP_ID=10\n"
                    "JOB_END=1554769000\n"
                    "JOB_EXIT=0\n"
                    "JOB_ID=20\n"
                    "JOB_NAME=job20\n"
                    "JOB_START=1554768000\n"
                    "JOB_STATUS=1\n"
                    "JOB_USER=user100\n"
                    "JOB_USER_ID=100\n"
            ),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "component_id": LDMSChrootTest.COMPONENT_ID,
                    "app_id": 10,
                    "job_id": 20,
                    "job_status": 1,
                    "user_id": 100,
                    "job_start": 1554768000,
                    "job_end": 1554769000,
                    "job_exit_status": 0,
                    "job_user": "user100",
                    "job_name": "job20",
                }
            }),
        ]
    ),
    # More SrcData() record for 2nd ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src("/var/run/ldms_jobinfo.data",
                    "JOB_APP_ID=101\n"
                    "JOB_END=1554779000\n"
                    "JOB_EXIT=0\n"
                    "JOB_ID=201\n"
                    "JOB_NAME=job201\n"
                    "JOB_START=1554778000\n"
                    "JOB_STATUS=1\n"
                    "JOB_USER=user1001\n"
                    "JOB_USER_ID=1001\n"
            ),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "component_id": LDMSChrootTest.COMPONENT_ID,
                    "app_id": 101,
                    "job_id": 201,
                    "job_status": 1,
                    "user_id": 1001,
                    "job_start": 1554778000,
                    "job_end": 1554779000,
                    "job_exit_status": 0,
                    "job_user": "user1001",
                    "job_name": "job201",
                }
            }),
        ]
    ),
]

class JobinfoTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"
    AUTO_JOB = False # We're testing jobinfo

    @classmethod
    def getLdmsConfig(cls):
        return """
        load name=test plugin=jobinfo
        config name=test component_id=%(COMPONENT_ID)d \
                         interval=%(INTERVAL_USEC)d \
                         offset=%(OFFSET)d
        """ % xvars(cls)

    @classmethod
    def getSrcData(cls, tidx):
        return src_data[tidx]


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

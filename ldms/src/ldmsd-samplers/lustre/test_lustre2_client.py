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
from lustre_metrics import LSTATS, LLITE_STATS, lstats_parse

HOSTNAME = socket.gethostname()
LDMSD_PREFIX = HOSTNAME + ":" + str(LDMSChrootTest.PORT)
INST_NAME = LDMSD_PREFIX + "/test"
SCHEMA_NAME = "lustre2_client"
DIR = "test_lustre2_client" # a work directory for this test, so that everything
                            # is in one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

# (prefix, [metrics], [objs] )

MDCS   =  ( "mdc"   , LSTATS      ,  [ "lustre-MDT0000-mdc-ffff99fc39c11800" ]  )
OSCS   =  ( "osc"   , LSTATS      ,  [ "lustre-OST0000-osc-ffff99fc39c11800" ]  )
LLITES =  ( "llite" , LLITE_STATS ,  [ "lustre-ffff99fc39c11800"             ]  )

PROCFILES = [ "/proc/fs/lustre/%s/%s/stats" % (p, o) \
                   for p, lm, x in (MDCS, OSCS, LLITES) for o in x ]

DEV_NO = dict()
NEXT_NO = 1

def patch_metric_name(name):
    global DEV_NO
    global NEXT_NO
    name = name.replace("#osc.lustre-", "#")
    name = name.replace("#llite.lustre-", "#")
    name = name.replace("#mdt.lustre-", "#")
    name = name.replace("#mdc.lustre-", "#")
    m = re.match(r'.*([0-9a-f]{16})$', name)
    if m:
        # replace the 16 hex with device number
        k = m.group(1)
        v = DEV_NO.setdefault(k, NEXT_NO)
        if v == NEXT_NO:
            NEXT_NO += 1
        v = "{:02d}".format(v)
        name = name.replace(k, v)
    return name

def client_metrics(proc_fs_lustre):
    # the `proc_fs_lustre` is the directory contain files from /proc/fs/lustre
    ret = dict()

    # -- initialize all metrics to 0 --
    ret.update({
            patch_metric_name("client.%s#%s.%s" % (m, p, o)): 0 \
                for p,lm,x in (MDCS, OSCS, LLITES) for o in x for m in lm
        })

    PROCENTRIES = \
        [ ("client.", "#%s.%s"%(p, o),
            proc_fs_lustre+"/"+p+"/"+o+"/stats", lm) \
                for p,lm,x in (MDCS, OSCS, LLITES) for o in x]

    for prefix, suffix, _path, fltr in PROCENTRIES:
        if not os.path.exists(_path):
            continue
        ret[patch_metric_name(prefix + "status" + suffix)] = 1
        mx = lstats_parse(_path, fltr)
        ret.update( { patch_metric_name(prefix + k + suffix): v for k,v in mx.iteritems() } )
    return ret


src_data = [
    # 1st SrcData() record for initialization + 1st ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src( x,
                 open(x.replace("/proc/fs/lustre", "proc-fs-lustre-client0"))
            ) for x in PROCFILES
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": client_metrics("proc-fs-lustre-client0")
            }),
        ]
    ),
    # More SrcData() record for 2nd ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src( x,
                 open(x.replace("/proc/fs/lustre", "proc-fs-lustre-client1"))
            ) for x in PROCFILES
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": client_metrics("proc-fs-lustre-client1")
            }),
        ]
    ),
]

class LustreClientTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def getPluginName(cls):
        return "lustre2_client"

    @classmethod
    def getPluginParams(cls):
        return { "osc": "*", "mdc": "*", "llite": "*" }

    @classmethod
    def getSrcData(cls, tidx):
        return src_data[tidx]

    def check(self, cset, lset):
        self.assertEqual(cset.instance_name, lset.instance_name)
        self.assertEqual(cset.schema_name, lset.schema_name)
        # check that their metrics are the same, but allowing `rate` metrics
        # to be different.
        d0 = dict(cset.metrics - lset.metrics)
        d1 = dict(lset.metrics - cset.metrics)
        D.cset = cset
        D.lset = lset
        D.d0 = d0
        D.d1 = d1
        self.assertEqual(set(d0.keys()), set(d1.keys()))
        for name in d0.keys():
            self.assertRegexpMatches(name, r'^client\.[^.]+\.rate#.*')


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

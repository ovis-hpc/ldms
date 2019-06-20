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

from ldmsd.chroot import D, LDMSData, LDMSChrootTest, try_sudo, xvars, \
                         Src, SrcData

HOSTNAME = socket.gethostname()
LDMSD_PREFIX = HOSTNAME + ":" + str(LDMSChrootTest.PORT)
INST_NAME = LDMSD_PREFIX + "/test"
SCHEMA_NAME = "procnfs"
DIR = "test_procnfs" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src_data = [
    # 1st SrcData() record for initialization + 1st ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src("/proc/net/rpc/nfs",
"""
net 0 0 0 0
rpc 3238 0 3238
proc4 61 1 0 0 0 1 0 0 0 0 0 3 0 0 0 0 0 0 6 29 6 1 0 0 0 0 0 2 0 3 1 5 0 0 0 0 0 0 0 2 1 0 3175 0 1 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0
"""
                ),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "rpc.retrans": 0,
                    "rpc.authrefresh": 3238,
                    "nfs3.getattr": 0,
                    "nfs3.setattr": 0,
                    "nfs3.lookup": 0,
                    "nfs3.access": 0,
                    "nfs3.readlink": 0,
                    "nfs3.read": 0,
                    "nfs3.write": 0,
                    "nfs3.create": 0,
                    "nfs3.mkdir": 0,
                    "nfs3.symlink": 0,
                    "nfs3.mknod": 0,
                    "nfs3.remove": 0,
                    "nfs3.rmdir": 0,
                    "nfs3.rename": 0,
                    "nfs3.link": 0,
                    "nfs3.readdir": 0,
                    "nfs3.readdirplus": 0,
                    "nfs3.fsstat": 0,
                    "nfs3.fsinfo": 0,
                    "nfs3.pathconf": 0,
                    "nfs3.commit": 0,
                    "nfs4.read": 0,
                    "nfs4.write": 0,
                    "nfs4.commit": 0,
                    "nfs4.open": 1,
                    "nfs4.open_confirm": 0,
                    "nfs4.open_noattr": 0,
                    "nfs4.open_downgrade": 0,
                    "nfs4.close": 0,
                    "nfs4.setattr": 0,
                    "nfs4.fsinfo": 3,
                    "nfs4.renew": 0,
                    "nfs4.setclientid": 0,
                    "nfs4.setclientid_confirm": 0,
                    "nfs4.lock": 0,
                    "nfs4.lockt": 0,
                    "nfs4.locku": 0,
                    "nfs4.access": 6,
                    "nfs4.getattr": 29,
                    "nfs4.lookup": 6,
                    "nfs4.lookup_root": 1,
                    "nfs4.remove": 0,
                    "nfs4.rename": 0,
                    "nfs4.link": 0,
                    "nfs4.symlink": 0,
                    "nfs4.create": 0,
                    "nfs4.pathconf": 2,
                    "nfs4.statfs": 0,
                    "nfs4.readlink": 3,
                    "nfs4.readdir": 1,
                    "nfs4.server_caps": 5,
                    "nfs4.delegreturn": 0,
                    "nfs4.getacl": 0,
                    "nfs4.setacl": 0,
                    "nfs4.fs_locations": 0,
                    "nfs4.release_lockowner": 0,
                    "nfs4.secinfo": 0,
                    "nfs4.fsid_present": 0,
                    "nfs4.exchange_id": 2,
                    "nfs4.create_session": 1,
                    "nfs4.destroy_session": 0,
                    "nfs4.sequence": 3175,
                    "nfs4.get_lease_time": 0,
                    "nfs4.reclaim_complete": 1,
                    "nfs4.layoutget": 0,
                    "nfs4.getdeviceinfo": 0,
                    "nfs4.layoutcommit": 0,
                    "nfs4.layoutreturn": 0,
                    "nfs4.secinfo_no_name": 1,
                    "nfs4.test_stateid": 0,
                    "nfs4.free_stateid": 0,
                    "nfs4.getdevicelist": 0,
                    "nfs4.bind_conn_to_session": 0,
                    "nfs4.destroy_clientid": 0,
                    "nfs4.seek": 0,
                    "nfs4.allocate": 0,
                    "nfs4.deallocate": 0,
                    "nfs4.layoutstats": 0,
                    "nfs4.clone": 0,
                    "nfs4.copy": 0,
                }
            }),
        ]
    ),
    # More SrcData() record for 2nd ldms update
    SrcData(
        [
            # 1 Src() per source file
            Src("/proc/net/rpc/nfs",
"""
net 0 0 0 0
rpc 3238 0 3238
proc4 61 1 0 0 0 1 0 0 0 0 0 3 0 0 0 0 0 0 6 39 6 1 0 0 0 0 0 2 0 3 1 5 0 0 0 0 0 0 0 2 1 0 4175 0 1 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0
"""
                ),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "rpc.retrans": 0,
                    "rpc.authrefresh": 3238,
                    "nfs3.getattr": 0,
                    "nfs3.setattr": 0,
                    "nfs3.lookup": 0,
                    "nfs3.access": 0,
                    "nfs3.readlink": 0,
                    "nfs3.read": 0,
                    "nfs3.write": 0,
                    "nfs3.create": 0,
                    "nfs3.mkdir": 0,
                    "nfs3.symlink": 0,
                    "nfs3.mknod": 0,
                    "nfs3.remove": 0,
                    "nfs3.rmdir": 0,
                    "nfs3.rename": 0,
                    "nfs3.link": 0,
                    "nfs3.readdir": 0,
                    "nfs3.readdirplus": 0,
                    "nfs3.fsstat": 0,
                    "nfs3.fsinfo": 0,
                    "nfs3.pathconf": 0,
                    "nfs3.commit": 0,
                    "nfs4.read": 0,
                    "nfs4.write": 0,
                    "nfs4.commit": 0,
                    "nfs4.open": 1,
                    "nfs4.open_confirm": 0,
                    "nfs4.open_noattr": 0,
                    "nfs4.open_downgrade": 0,
                    "nfs4.close": 0,
                    "nfs4.setattr": 0,
                    "nfs4.fsinfo": 3,
                    "nfs4.renew": 0,
                    "nfs4.setclientid": 0,
                    "nfs4.setclientid_confirm": 0,
                    "nfs4.lock": 0,
                    "nfs4.lockt": 0,
                    "nfs4.locku": 0,
                    "nfs4.access": 6,
                    "nfs4.getattr": 39,
                    "nfs4.lookup": 6,
                    "nfs4.lookup_root": 1,
                    "nfs4.remove": 0,
                    "nfs4.rename": 0,
                    "nfs4.link": 0,
                    "nfs4.symlink": 0,
                    "nfs4.create": 0,
                    "nfs4.pathconf": 2,
                    "nfs4.statfs": 0,
                    "nfs4.readlink": 3,
                    "nfs4.readdir": 1,
                    "nfs4.server_caps": 5,
                    "nfs4.delegreturn": 0,
                    "nfs4.getacl": 0,
                    "nfs4.setacl": 0,
                    "nfs4.fs_locations": 0,
                    "nfs4.release_lockowner": 0,
                    "nfs4.secinfo": 0,
                    "nfs4.fsid_present": 0,
                    "nfs4.exchange_id": 2,
                    "nfs4.create_session": 1,
                    "nfs4.destroy_session": 0,
                    "nfs4.sequence": 4175,
                    "nfs4.get_lease_time": 0,
                    "nfs4.reclaim_complete": 1,
                    "nfs4.layoutget": 0,
                    "nfs4.getdeviceinfo": 0,
                    "nfs4.layoutcommit": 0,
                    "nfs4.layoutreturn": 0,
                    "nfs4.secinfo_no_name": 1,
                    "nfs4.test_stateid": 0,
                    "nfs4.free_stateid": 0,
                    "nfs4.getdevicelist": 0,
                    "nfs4.bind_conn_to_session": 0,
                    "nfs4.destroy_clientid": 0,
                    "nfs4.seek": 0,
                    "nfs4.allocate": 0,
                    "nfs4.deallocate": 0,
                    "nfs4.layoutstats": 0,
                    "nfs4.clone": 0,
                    "nfs4.copy": 0,
                }
            }),
        ]
    ),
]

class ProcnfsTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def getPluginName(cls):
        return "procnfs"

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

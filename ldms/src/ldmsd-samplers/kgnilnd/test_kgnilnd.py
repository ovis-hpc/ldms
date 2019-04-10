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
SCHEMA_NAME = "kgnilnd"
DIR = "test_kgnilnd" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src_data = [
    # 1st SrcData() record for initialization + 1st ldms update
    SrcData(
        [
            # 1 Src(PATH, CONTENT) per source file
            Src("/proc/kgnilnd/stats",
                    "time: 1554894618.892172\n"
                    "ntx: 0\n"
                    "npeers: 15\n"
                    "nconns: 7\n"
                    "nEPs: 7\n"
                    "ndgrams: 4\n"
                    "nfmablk: 1\n"
                    "n_mdd: 1\n"
                    "n_mdd_held: 0\n"
                    "n_eager_allocs: 0\n"
                    "GART map bytes: 74309632\n"
                    "TX queued maps: 0\n"
                    "TX phys nmaps: 0\n"
                    "TX phys bytes: 0\n"
                    "TX virt nmaps: 0\n"
                    "TX virt bytes: 0\n"
                    "RDMAQ bytes_auth: 7020988896\n"
                    "RDMAQ bytes_left: 9223372029833786911\n"
                    "RDMAQ nstalls: 0\n"
                    "dev mutex delay: 3569\n"
                    "dev n_yield: 4960533\n"
                    "dev n_schedule: -1609036116\n"
                    "SMSG fast_try: 151364744\n"
                    "SMSG fast_ok: 151364214\n"
                    "SMSG fast_block: 21288675\n"
                    "SMSG ntx: 177614377\n"
                    "SMSG tx_bytes: 75792495969\n"
                    "SMSG nrx: 170165045\n"
                    "SMSG rx_bytes: 71930531529\n"
                    "RDMA ntx: 1807923\n"
                    "RDMA tx_bytes: 146269342238\n"
                    "RDMA nrx: 1298480\n"
                    "RDMA rx_bytes: 15607145934\n"
                    "VMAP short: 0\n"
                    "VMAP cksum: 0\n"
                    "KMAP short: 1103904\n"
                    "RDMA REV length: 0\n"
                    "RDMA REV offset: 0\n"
                    "RDMA REV copy: 0\n"
                ),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "time": 1554894618, # only retain the seconds part
                    "ntx": 0,
                    "npeers": 15,
                    "nconns": 7,
                    "nEPs": 7,
                    "ndgrams": 4,
                    "nfmablk": 1,
                    "n_mdd": 1,
                    "n_mdd_held": 0,
                    "n_eager_allocs": 0,
                    "GART_map_bytes": 74309632,
                    "TX_queued_maps": 0,
                    "TX_phys_nmaps": 0,
                    "TX_phys_bytes": 0,
                    "TX_virt_nmaps": 0,
                    "TX_virt_bytes": 0,
                    "RDMAQ_bytes_auth": 7020988896,
                    "RDMAQ_bytes_left": 9223372029833786911,
                    "RDMAQ_nstalls": 0,
                    "dev_mutex_delay": 3569,
                    "dev_n_yield": 4960533,
                    "dev_n_schedule": -1609036116,
                    "SMSG_fast_try": 151364744,
                    "SMSG_fast_ok": 151364214,
                    "SMSG_fast_block": 21288675,
                    "SMSG_ntx": 177614377,
                    "SMSG_tx_bytes": 75792495969,
                    "SMSG_nrx": 170165045,
                    "SMSG_rx_bytes": 71930531529,
                    "RDMA_ntx": 1807923,
                    "RDMA_tx_bytes": 146269342238,
                    "RDMA_nrx": 1298480,
                    "RDMA_rx_bytes": 15607145934,
                    "VMAP_short": 0,
                    "VMAP_cksum": 0,
                    "KMAP_short": 1103904,
                    "RDMA_REV_length": 0,
                    "RDMA_REV_offset": 0,
                    "RDMA_REV_copy": 0,
                }
            }),
        ]
    ),
    # More SrcData() record for 2nd ldms update
    SrcData(
        [
            # 1 Src(PATH, CONTENT) per source file
            Src("/proc/kgnilnd/stats",
                    "time: 1554894718.892172\n"
                    "ntx: 0\n"
                    "npeers: 15\n"
                    "nconns: 7\n"
                    "nEPs: 7\n"
                    "ndgrams: 4\n"
                    "nfmablk: 1\n"
                    "n_mdd: 1\n"
                    "n_mdd_held: 0\n"
                    "n_eager_allocs: 0\n"
                    "GART map bytes: 74309632\n"
                    "TX queued maps: 1\n"
                    "TX phys nmaps: 2\n"
                    "TX phys bytes: 3\n"
                    "TX virt nmaps: 4\n"
                    "TX virt bytes: 5\n"
                    "RDMAQ bytes_auth: 7020988896\n"
                    "RDMAQ bytes_left: 9223372029833786911\n"
                    "RDMAQ nstalls: 0\n"
                    "dev mutex delay: 3569\n"
                    "dev n_yield: 4960533\n"
                    "dev n_schedule: -1609036116\n"
                    "SMSG fast_try: 151364744\n"
                    "SMSG fast_ok: 151364214\n"
                    "SMSG fast_block: 21288675\n"
                    "SMSG ntx: 177614377\n"
                    "SMSG tx_bytes: 75792495969\n"
                    "SMSG nrx: 170165045\n"
                    "SMSG rx_bytes: 71930531529\n"
                    "RDMA ntx: 1807923\n"
                    "RDMA tx_bytes: 146269342238\n"
                    "RDMA nrx: 1298480\n"
                    "RDMA rx_bytes: 15607145934\n"
                    "VMAP short: 0\n"
                    "VMAP cksum: 0\n"
                    "KMAP short: 1103904\n"
                    "RDMA REV length: 10\n"
                    "RDMA REV offset: 11\n"
                    "RDMA REV copy: 12\n"
                ),
        ],
        [
            # 1 LDMSData() per set
            LDMSData({
                "instance_name": INST_NAME,
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "time": 1554894718, # only retain the seconds part
                    "ntx": 0,
                    "npeers": 15,
                    "nconns": 7,
                    "nEPs": 7,
                    "ndgrams": 4,
                    "nfmablk": 1,
                    "n_mdd": 1,
                    "n_mdd_held": 0,
                    "n_eager_allocs": 0,
                    "GART_map_bytes": 74309632,
                    "TX_queued_maps": 1,
                    "TX_phys_nmaps": 2,
                    "TX_phys_bytes": 3,
                    "TX_virt_nmaps": 4,
                    "TX_virt_bytes": 5,
                    "RDMAQ_bytes_auth": 7020988896,
                    "RDMAQ_bytes_left": 9223372029833786911,
                    "RDMAQ_nstalls": 0,
                    "dev_mutex_delay": 3569,
                    "dev_n_yield": 4960533,
                    "dev_n_schedule": -1609036116,
                    "SMSG_fast_try": 151364744,
                    "SMSG_fast_ok": 151364214,
                    "SMSG_fast_block": 21288675,
                    "SMSG_ntx": 177614377,
                    "SMSG_tx_bytes": 75792495969,
                    "SMSG_nrx": 170165045,
                    "SMSG_rx_bytes": 71930531529,
                    "RDMA_ntx": 1807923,
                    "RDMA_tx_bytes": 146269342238,
                    "RDMA_nrx": 1298480,
                    "RDMA_rx_bytes": 15607145934,
                    "VMAP_short": 0,
                    "VMAP_cksum": 0,
                    "KMAP_short": 1103904,
                    "RDMA_REV_length": 10,
                    "RDMA_REV_offset": 11,
                    "RDMA_REV_copy": 12,
                }
            }),
        ]
    ),
]

class KgnilndTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def getPluginName(cls):
        return "kgnilnd"

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

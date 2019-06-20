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
SCHEMA_NAME = "vmstat"
DIR = "test_vmstat" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src_data = [
SrcData(Src("/proc/vmstat",
"""\
nr_free_pages 7004937
nr_zone_inactive_anon 105239
nr_zone_active_anon 545377
nr_zone_inactive_file 204018
nr_zone_active_file 149069
nr_zone_unevictable 16
nr_zone_write_pending 79
nr_mlock 16
nr_page_table_pages 12785
nr_kernel_stack 14672
nr_bounce 0
nr_zspages 0
nr_free_cma 0
numa_hit 9012402
numa_miss 0
numa_foreign 0
numa_interleave 57210
numa_local 9012402
numa_other 0
nr_inactive_anon 105239
nr_active_anon 545377
nr_inactive_file 204018
nr_active_file 149069
nr_unevictable 16
nr_slab_reclaimable 22199
nr_slab_unreclaimable 13917
nr_isolated_anon 0
nr_isolated_file 0
workingset_refault 0
workingset_activate 0
workingset_nodereclaim 0
nr_anon_pages 545100
nr_mapped 167156
nr_file_pages 458664
nr_dirty 79
nr_writeback 0
nr_writeback_temp 0
nr_shmem 105580
nr_shmem_hugepages 0
nr_shmem_pmdmapped 0
nr_anon_transparent_hugepages 0
nr_unstable 0
nr_vmscan_write 0
nr_vmscan_immediate_reclaim 0
nr_dirtied 167700
nr_written 146886
nr_dirty_threshold 1458118
nr_dirty_background_threshold 728169
pgpgin 1233050
pgpgout 670824
pswpin 0
pswpout 0
pgalloc_dma 2
pgalloc_dma32 57
pgalloc_normal 9156027
pgalloc_movable 0
allocstall_dma 0
allocstall_dma32 0
allocstall_normal 0
allocstall_movable 0
pgskip_dma 0
pgskip_dma32 0
pgskip_normal 0
pgskip_movable 0
pgfree 16162133
pgactivate 386545
pgdeactivate 0
pglazyfree 0
pgfault 9637312
pgmajfault 5590
pglazyfreed 0
pgrefill 0
pgsteal_kswapd 0
pgsteal_direct 0
pgscan_kswapd 0
pgscan_direct 0
pgscan_direct_throttle 0
zone_reclaim_failed 0
pginodesteal 0
slabs_scanned 0
kswapd_inodesteal 0
kswapd_low_wmark_hit_quickly 0
kswapd_high_wmark_hit_quickly 0
pageoutrun 0
pgrotated 34
drop_pagecache 0
drop_slab 0
oom_kill 0
numa_pte_updates 0
numa_huge_pte_updates 0
numa_hint_faults 0
numa_hint_faults_local 0
numa_pages_migrated 0
pgmigrate_success 0
pgmigrate_fail 0
compact_migrate_scanned 0
compact_free_scanned 0
compact_isolated 0
compact_stall 0
compact_fail 0
compact_success 0
compact_daemon_wake 0
compact_daemon_migrate_scanned 0
compact_daemon_free_scanned 0
htlb_buddy_alloc_success 0
htlb_buddy_alloc_fail 0
unevictable_pgs_culled 10707
unevictable_pgs_scanned 0
unevictable_pgs_rescued 9080
unevictable_pgs_mlocked 11180
unevictable_pgs_munlocked 11164
unevictable_pgs_cleared 0
unevictable_pgs_stranded 0
thp_fault_alloc 0
thp_fault_fallback 0
thp_collapse_alloc 0
thp_collapse_alloc_failed 0
thp_file_alloc 0
thp_file_mapped 0
thp_split_page 0
thp_split_page_failed 0
thp_deferred_split_page 0
thp_split_pmd 0
thp_split_pud 0
thp_zero_page_alloc 0
thp_zero_page_alloc_failed 0
thp_swpout 0
thp_swpout_fallback 0
balloon_inflate 0
balloon_deflate 0
balloon_migrate 0
swap_ra 0
swap_ra_hit 0
"""),
LDMSData({
    "instance_name": INST_NAME,
    "schema_name": SCHEMA_NAME,
    "metrics": {
        "nr_free_pages": 7004937,
        "nr_zone_inactive_anon": 105239,
        "nr_zone_active_anon": 545377,
        "nr_zone_inactive_file": 204018,
        "nr_zone_active_file": 149069,
        "nr_zone_unevictable": 16,
        "nr_zone_write_pending": 79,
        "nr_mlock": 16,
        "nr_page_table_pages": 12785,
        "nr_kernel_stack": 14672,
        "nr_bounce": 0,
        "nr_zspages": 0,
        "nr_free_cma": 0,
        "numa_hit": 9012402,
        "numa_miss": 0,
        "numa_foreign": 0,
        "numa_interleave": 57210,
        "numa_local": 9012402,
        "numa_other": 0,
        "nr_inactive_anon": 105239,
        "nr_active_anon": 545377,
        "nr_inactive_file": 204018,
        "nr_active_file": 149069,
        "nr_unevictable": 16,
        "nr_slab_reclaimable": 22199,
        "nr_slab_unreclaimable": 13917,
        "nr_isolated_anon": 0,
        "nr_isolated_file": 0,
        "workingset_refault": 0,
        "workingset_activate": 0,
        "workingset_nodereclaim": 0,
        "nr_anon_pages": 545100,
        "nr_mapped": 167156,
        "nr_file_pages": 458664,
        "nr_dirty": 79,
        "nr_writeback": 0,
        "nr_writeback_temp": 0,
        "nr_shmem": 105580,
        "nr_shmem_hugepages": 0,
        "nr_shmem_pmdmapped": 0,
        "nr_anon_transparent_hugepages": 0,
        "nr_unstable": 0,
        "nr_vmscan_write": 0,
        "nr_vmscan_immediate_reclaim": 0,
        "nr_dirtied": 167700,
        "nr_written": 146886,
        "nr_dirty_threshold": 1458118,
        "nr_dirty_background_threshold": 728169,
        "pgpgin": 1233050,
        "pgpgout": 670824,
        "pswpin": 0,
        "pswpout": 0,
        "pgalloc_dma": 2,
        "pgalloc_dma32": 57,
        "pgalloc_normal": 9156027,
        "pgalloc_movable": 0,
        "allocstall_dma": 0,
        "allocstall_dma32": 0,
        "allocstall_normal": 0,
        "allocstall_movable": 0,
        "pgskip_dma": 0,
        "pgskip_dma32": 0,
        "pgskip_normal": 0,
        "pgskip_movable": 0,
        "pgfree": 16162133,
        "pgactivate": 386545,
        "pgdeactivate": 0,
        "pglazyfree": 0,
        "pgfault": 9637312,
        "pgmajfault": 5590,
        "pglazyfreed": 0,
        "pgrefill": 0,
        "pgsteal_kswapd": 0,
        "pgsteal_direct": 0,
        "pgscan_kswapd": 0,
        "pgscan_direct": 0,
        "pgscan_direct_throttle": 0,
        "zone_reclaim_failed": 0,
        "pginodesteal": 0,
        "slabs_scanned": 0,
        "kswapd_inodesteal": 0,
        "kswapd_low_wmark_hit_quickly": 0,
        "kswapd_high_wmark_hit_quickly": 0,
        "pageoutrun": 0,
        "pgrotated": 34,
        "drop_pagecache": 0,
        "drop_slab": 0,
        "oom_kill": 0,
        "numa_pte_updates": 0,
        "numa_huge_pte_updates": 0,
        "numa_hint_faults": 0,
        "numa_hint_faults_local": 0,
        "numa_pages_migrated": 0,
        "pgmigrate_success": 0,
        "pgmigrate_fail": 0,
        "compact_migrate_scanned": 0,
        "compact_free_scanned": 0,
        "compact_isolated": 0,
        "compact_stall": 0,
        "compact_fail": 0,
        "compact_success": 0,
        "compact_daemon_wake": 0,
        "compact_daemon_migrate_scanned": 0,
        "compact_daemon_free_scanned": 0,
        "htlb_buddy_alloc_success": 0,
        "htlb_buddy_alloc_fail": 0,
        "unevictable_pgs_culled": 10707,
        "unevictable_pgs_scanned": 0,
        "unevictable_pgs_rescued": 9080,
        "unevictable_pgs_mlocked": 11180,
        "unevictable_pgs_munlocked": 11164,
        "unevictable_pgs_cleared": 0,
        "unevictable_pgs_stranded": 0,
        "thp_fault_alloc": 0,
        "thp_fault_fallback": 0,
        "thp_collapse_alloc": 0,
        "thp_collapse_alloc_failed": 0,
        "thp_file_alloc": 0,
        "thp_file_mapped": 0,
        "thp_split_page": 0,
        "thp_split_page_failed": 0,
        "thp_deferred_split_page": 0,
        "thp_split_pmd": 0,
        "thp_split_pud": 0,
        "thp_zero_page_alloc": 0,
        "thp_zero_page_alloc_failed": 0,
        "thp_swpout": 0,
        "thp_swpout_fallback": 0,
        "balloon_inflate": 0,
        "balloon_deflate": 0,
        "balloon_migrate": 0,
        "swap_ra": 0,
        "swap_ra_hit": 0,
    }
})),
SrcData(Src("/proc/vmstat",
"""\
nr_free_pages 7004632
nr_zone_inactive_anon 105201
nr_zone_active_anon 545516
nr_zone_inactive_file 204017
nr_zone_active_file 149127
nr_zone_unevictable 16
nr_zone_write_pending 68
nr_mlock 16
nr_page_table_pages 12795
nr_kernel_stack 14832
nr_bounce 0
nr_zspages 0
nr_free_cma 0
numa_hit 9075294
numa_miss 0
numa_foreign 0
numa_interleave 57210
numa_local 9075294
numa_other 0
nr_inactive_anon 105201
nr_active_anon 545516
nr_inactive_file 204017
nr_active_file 149127
nr_unevictable 16
nr_slab_reclaimable 22193
nr_slab_unreclaimable 13938
nr_isolated_anon 0
nr_isolated_file 0
workingset_refault 0
workingset_activate 0
workingset_nodereclaim 0
nr_anon_pages 545232
nr_mapped 167180
nr_file_pages 458694
nr_dirty 68
nr_writeback 0
nr_writeback_temp 0
nr_shmem 105552
nr_shmem_hugepages 0
nr_shmem_pmdmapped 0
nr_anon_transparent_hugepages 0
nr_unstable 0
nr_vmscan_write 0
nr_vmscan_immediate_reclaim 0
nr_dirtied 168273
nr_written 147466
nr_dirty_threshold 1458068
nr_dirty_background_threshold 728144
pgpgin 1233050
pgpgout 674140
pswpin 0
pswpout 0
pgalloc_dma 2
pgalloc_dma32 57
pgalloc_normal 9219183
pgalloc_movable 0
allocstall_dma 0
allocstall_dma32 0
allocstall_normal 0
allocstall_movable 0
pgskip_dma 0
pgskip_dma32 0
pgskip_normal 0
pgskip_movable 0
pgfree 16225018
pgactivate 386688
pgdeactivate 0
pglazyfree 0
pgfault 9667506
pgmajfault 5590
pglazyfreed 0
pgrefill 0
pgsteal_kswapd 0
pgsteal_direct 0
pgscan_kswapd 0
pgscan_direct 0
pgscan_direct_throttle 0
zone_reclaim_failed 0
pginodesteal 0
slabs_scanned 0
kswapd_inodesteal 0
kswapd_low_wmark_hit_quickly 0
kswapd_high_wmark_hit_quickly 0
pageoutrun 0
pgrotated 34
drop_pagecache 0
drop_slab 0
oom_kill 0
numa_pte_updates 0
numa_huge_pte_updates 0
numa_hint_faults 0
numa_hint_faults_local 0
numa_pages_migrated 0
pgmigrate_success 0
pgmigrate_fail 0
compact_migrate_scanned 0
compact_free_scanned 0
compact_isolated 0
compact_stall 0
compact_fail 0
compact_success 0
compact_daemon_wake 0
compact_daemon_migrate_scanned 0
compact_daemon_free_scanned 0
htlb_buddy_alloc_success 0
htlb_buddy_alloc_fail 0
unevictable_pgs_culled 10707
unevictable_pgs_scanned 0
unevictable_pgs_rescued 9080
unevictable_pgs_mlocked 11180
unevictable_pgs_munlocked 11164
unevictable_pgs_cleared 0
unevictable_pgs_stranded 0
thp_fault_alloc 0
thp_fault_fallback 0
thp_collapse_alloc 0
thp_collapse_alloc_failed 0
thp_file_alloc 0
thp_file_mapped 0
thp_split_page 0
thp_split_page_failed 0
thp_deferred_split_page 0
thp_split_pmd 0
thp_split_pud 0
thp_zero_page_alloc 0
thp_zero_page_alloc_failed 0
thp_swpout 0
thp_swpout_fallback 0
balloon_inflate 0
balloon_deflate 0
balloon_migrate 0
swap_ra 0
swap_ra_hit 0
"""),
LDMSData({
    "instance_name": INST_NAME,
    "schema_name": SCHEMA_NAME,
    "metrics": {
        "nr_free_pages": 7004632,
        "nr_zone_inactive_anon": 105201,
        "nr_zone_active_anon": 545516,
        "nr_zone_inactive_file": 204017,
        "nr_zone_active_file": 149127,
        "nr_zone_unevictable": 16,
        "nr_zone_write_pending": 68,
        "nr_mlock": 16,
        "nr_page_table_pages": 12795,
        "nr_kernel_stack": 14832,
        "nr_bounce": 0,
        "nr_zspages": 0,
        "nr_free_cma": 0,
        "numa_hit": 9075294,
        "numa_miss": 0,
        "numa_foreign": 0,
        "numa_interleave": 57210,
        "numa_local": 9075294,
        "numa_other": 0,
        "nr_inactive_anon": 105201,
        "nr_active_anon": 545516,
        "nr_inactive_file": 204017,
        "nr_active_file": 149127,
        "nr_unevictable": 16,
        "nr_slab_reclaimable": 22193,
        "nr_slab_unreclaimable": 13938,
        "nr_isolated_anon": 0,
        "nr_isolated_file": 0,
        "workingset_refault": 0,
        "workingset_activate": 0,
        "workingset_nodereclaim": 0,
        "nr_anon_pages": 545232,
        "nr_mapped": 167180,
        "nr_file_pages": 458694,
        "nr_dirty": 68,
        "nr_writeback": 0,
        "nr_writeback_temp": 0,
        "nr_shmem": 105552,
        "nr_shmem_hugepages": 0,
        "nr_shmem_pmdmapped": 0,
        "nr_anon_transparent_hugepages": 0,
        "nr_unstable": 0,
        "nr_vmscan_write": 0,
        "nr_vmscan_immediate_reclaim": 0,
        "nr_dirtied": 168273,
        "nr_written": 147466,
        "nr_dirty_threshold": 1458068,
        "nr_dirty_background_threshold": 728144,
        "pgpgin": 1233050,
        "pgpgout": 674140,
        "pswpin": 0,
        "pswpout": 0,
        "pgalloc_dma": 2,
        "pgalloc_dma32": 57,
        "pgalloc_normal": 9219183,
        "pgalloc_movable": 0,
        "allocstall_dma": 0,
        "allocstall_dma32": 0,
        "allocstall_normal": 0,
        "allocstall_movable": 0,
        "pgskip_dma": 0,
        "pgskip_dma32": 0,
        "pgskip_normal": 0,
        "pgskip_movable": 0,
        "pgfree": 16225018,
        "pgactivate": 386688,
        "pgdeactivate": 0,
        "pglazyfree": 0,
        "pgfault": 9667506,
        "pgmajfault": 5590,
        "pglazyfreed": 0,
        "pgrefill": 0,
        "pgsteal_kswapd": 0,
        "pgsteal_direct": 0,
        "pgscan_kswapd": 0,
        "pgscan_direct": 0,
        "pgscan_direct_throttle": 0,
        "zone_reclaim_failed": 0,
        "pginodesteal": 0,
        "slabs_scanned": 0,
        "kswapd_inodesteal": 0,
        "kswapd_low_wmark_hit_quickly": 0,
        "kswapd_high_wmark_hit_quickly": 0,
        "pageoutrun": 0,
        "pgrotated": 34,
        "drop_pagecache": 0,
        "drop_slab": 0,
        "oom_kill": 0,
        "numa_pte_updates": 0,
        "numa_huge_pte_updates": 0,
        "numa_hint_faults": 0,
        "numa_hint_faults_local": 0,
        "numa_pages_migrated": 0,
        "pgmigrate_success": 0,
        "pgmigrate_fail": 0,
        "compact_migrate_scanned": 0,
        "compact_free_scanned": 0,
        "compact_isolated": 0,
        "compact_stall": 0,
        "compact_fail": 0,
        "compact_success": 0,
        "compact_daemon_wake": 0,
        "compact_daemon_migrate_scanned": 0,
        "compact_daemon_free_scanned": 0,
        "htlb_buddy_alloc_success": 0,
        "htlb_buddy_alloc_fail": 0,
        "unevictable_pgs_culled": 10707,
        "unevictable_pgs_scanned": 0,
        "unevictable_pgs_rescued": 9080,
        "unevictable_pgs_mlocked": 11180,
        "unevictable_pgs_munlocked": 11164,
        "unevictable_pgs_cleared": 0,
        "unevictable_pgs_stranded": 0,
        "thp_fault_alloc": 0,
        "thp_fault_fallback": 0,
        "thp_collapse_alloc": 0,
        "thp_collapse_alloc_failed": 0,
        "thp_file_alloc": 0,
        "thp_file_mapped": 0,
        "thp_split_page": 0,
        "thp_split_page_failed": 0,
        "thp_deferred_split_page": 0,
        "thp_split_pmd": 0,
        "thp_split_pud": 0,
        "thp_zero_page_alloc": 0,
        "thp_zero_page_alloc_failed": 0,
        "thp_swpout": 0,
        "thp_swpout_fallback": 0,
        "balloon_inflate": 0,
        "balloon_deflate": 0,
        "balloon_migrate": 0,
        "swap_ra": 0,
        "swap_ra_hit": 0,
    }
})),
]

class VMStatTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def getPluginName(cls):
        return "vmstat"

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
            filename = DIR + "/test_vmstat.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

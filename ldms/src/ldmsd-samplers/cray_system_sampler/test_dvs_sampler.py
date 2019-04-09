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
SCHEMA_NAME = "dvs_sampler"
DIR = "test_dvs_sampler" # a work directory for this test, so that everything is in
                     # one place
if not os.path.exists(DIR):
    os.mkdir(DIR)

src_data = [
    SrcData([
            Src("/proc/fs/dvs/mounts/0/mount",
"""\
local-mount /var/opt/cray/imps
remote-path /var/opt/cray/imps
options (ro,blksize=524288,statsfile=/proc/fs/dvs/mounts/1/stats,attrcache_timeout=14400,nodwfs,nodwcfs,noparallelwrite,nomultifsync,cache,nodatasync,noclosesync,retry,failover,userenv,clusterfs,killprocess,noatomic,nodeferopens,no_distribute_create_ops,no_ro_cache,loadbalance,maxnodes=1,nnodes=1,nomagic,nohash_on_nid,hash=modulo,nodefile=/proc/fs/dvs/mounts/1/nodenames,nodename=c0-0c0s0n2)
active_nodes c0-0c0s0n2
inactive_nodes
loadbalance_node c0-0c0s0n2
remote-magic 0x6969
"""),
            Src("/proc/fs/dvs/mounts/1/mount",
"""\
local-mount /test/path
remote-path /test/path
options (ro,blksize=524288,statsfile=/proc/fs/dvs/mounts/1/stats,attrcache_timeout=14400,nodwfs,nodwcfs,noparallelwrite,nomultifsync,cache,nodatasync,noclosesync,retry,failover,userenv,clusterfs,killprocess,noatomic,nodeferopens,no_distribute_create_ops,no_ro_cache,loadbalance,maxnodes=1,nnodes=1,nomagic,nohash_on_nid,hash=modulo,nodefile=/proc/fs/dvs/mounts/1/nodenames,nodename=c0-0c0s0n2)
active_nodes c0-0c0s0n2
inactive_nodes
loadbalance_node c0-0c0s0n2
remote-magic 0x6969
"""),
            Src("/proc/fs/dvs/mounts/0/stats",
"""\
RQ_LOOKUP: 974286 0 0 0 0.000 0.000
RQ_OPEN: 59362 0 0 0 0.000 0.000
RQ_CLOSE: 59360 0 0 0 0.000 0.000
RQ_READDIR: 113550 0 0 0 0.000 0.000
RQ_CREATE: 0 0 0 0 0.000 0.000
RQ_UNLINK: 0 0 0 0 0.000 0.000
RQ_IOCTL: 0 0 0 0 0.000 0.000
RQ_FLUSH: 0 0 0 0 0.000 0.000
RQ_FSYNC: 0 0 0 0 0.000 0.000
RQ_FASYNC: 0 0 0 0 0.000 0.000
RQ_LOCK: 0 0 0 0 0.000 0.000
RQ_LINK: 0 0 0 0 0.000 0.000
RQ_SYMLINK: 0 0 0 0 0.000 0.000
RQ_MKDIR: 0 0 0 0 0.000 0.000
RQ_RMDIR: 0 0 0 0 0.000 0.000
RQ_MKNOD: 0 0 0 0 0.000 0.000
RQ_RENAME: 0 0 0 0 0.000 0.000
RQ_READLINK: 2 0 0 0 0.000 0.000
RQ_TRUNCATE: 0 0 0 0 0.000 0.000
RQ_SETATTR: 0 0 0 0 0.000 0.000
RQ_GETATTR: 72 0 0 0 0.000 0.000
RQ_PARALLEL_READ: 0 0 0 0 0.000 0.000
RQ_PARALLEL_WRITE: 0 0 0 0 0.000 0.000
RQ_STATFS: 21 0 0 0 0.000 0.000
RQ_READPAGE_ASYNC: 0 0 0 0 0.000 0.000
RQ_READPAGE_DATA: 0 0 0 0 0.000 0.000
RQ_GETEOI: 0 0 0 0 0.000 0.000
RQ_SETXATTR: 0 0 0 0 0.000 0.000
RQ_GETXATTR: 0 0 0 0 0.000 0.000
RQ_LISTXATTR: 0 0 0 0 0.000 0.000
RQ_REMOVEXATTR: 0 0 0 0 0.000 0.000
RQ_VERIFYFS: 0 0 0 0 0.000 0.000
RQ_RO_CACHE_DISABLE: 0 0 0 0 0.000 0.000
RQ_PERMISSION: 4 0 0 0 0.000 0.000
RQ_SYNC_UPDATE: 0 0 0 0 0.000 0.000
RQ_READPAGES_RQ: 29360 0 0 0 0.000 0.000
RQ_READPAGES_RP: 0 0 0 0 0.000 0.000
RQ_WRITEPAGES_RQ: 0 0 0 0 0.000 0.000
RQ_WRITEPAGES_RP: 0 0 0 0 0.000 0.000
llseek: 0 0 0.000 0.000
read: 0 0 0.000 0.000
aio_read: 951107 8 0.000 15.004
write: 0 0 0.000 0.000
aio_write: 0 0 0.000 0.000
readdir: 113550 0 0.000 0.796
unlocked_ioctl: 0 2 0.000 0.000
mmap: 0 0 0.000 0.000
open: 59362 0 0.000 0.012
flush: 228300 0 0.000 0.004
release: 59360 0 0.000 0.004
fsync: 0 0 0.000 0.000
fasync: 0 0 0.000 0.000
lock: 0 0 0.000 0.000
flock: 0 0 0.000 0.000
writepage: 0 0 0.000 0.000
writepages: 0 0 0.000 0.000
readpage: 160 0 0.000 0.004
readpages: 26627 0 0.000 0.744
write_begin: 0 0 0.000 0.000
write_end: 0 0 0.000 0.000
direct_io: 0 0 0.000 0.000
statfs: 21 0 0.000 0.004
put_super: 0 0 0.000 0.000
write_super: 0 0 0.000 0.000
evict_inode: 3 0 0.000 0.000
show_options: 81525 0 0.000 0.004
d_create: 0 0 0.000 0.000
d_lookup: 974281 0 0.000 0.016
d_link: 0 0 0.000 0.000
d_unlink: 0 0 0.000 0.000
d_symlink: 0 0 0.000 0.000
d_mkdir: 0 0 0.000 0.000
d_rmdir: 0 0 0.000 0.000
d_mknod: 0 0 0.000 0.000
d_rename: 0 0 0.000 0.000
d_truncate: 0 0 0.000 0.000
d_permission: 1149779 1090389 0.004 0.004
d_setattr: 0 0 0.000 0.000
d_getattr: 172814 0 0.004 0.004
d_setxattr: 0 0 0.000 0.000
d_getxattr: 0 0 0.000 0.000
d_listxattr: 0 0 0.000 0.000
d_removexattr: 0 0 0.000 0.000
f_create: 0 0 0.000 0.000
f_link: 0 0 0.000 0.000
f_unlink: 0 0 0.000 0.000
f_symlink: 0 0 0.000 0.000
f_mkdir: 0 0 0.000 0.000
f_rmdir: 0 0 0.000 0.000
f_mknod: 0 0 0.000 0.000
f_rename: 0 0 0.000 0.000
f_truncate: 0 0 0.000 0.000
f_permission: 2 0 0.000 0.000
f_setattr: 0 0 0.000 0.000
f_getattr: 858514 0 0.000 0.004
f_setxattr: 0 0 0.000 0.000
f_getxattr: 0 0 0.000 0.000
f_listxattr: 0 0 0.000 0.000
f_removexattr: 0 0 0.000 0.000
l_readlink: 0 0 0.000 0.000
l_follow_link: 0 0 0.000 0.000
l_put_link: 0 0 0.000 0.000
l_setattr: 0 0 0.000 0.000
l_getattr: 59088 0 0.000 0.004
d_revalidate: 113449 0 0.000 0.000
read_min_max: 0 0
write_min_max: 0 0
IPC requests: 0 0
IPC async requests: 0 0
IPC replies: 0 0
Open files: 2
Inodes created: 968132
Inodes removed: 3
"""),
            Src("/proc/fs/dvs/mounts/1/stats",
"""\
RQ_LOOKUP: 974286 0 0 0 0.000 0.000
RQ_OPEN: 59362 0 0 0 0.000 0.000
RQ_CLOSE: 59360 0 0 0 0.000 0.000
RQ_READDIR: 113550 0 0 0 0.000 0.000
RQ_CREATE: 0 0 0 0 0.000 0.000
RQ_UNLINK: 0 0 0 0 0.000 0.000
RQ_IOCTL: 0 0 0 0 0.000 0.000
RQ_FLUSH: 0 0 0 0 0.000 0.000
RQ_FSYNC: 0 0 0 0 0.000 0.000
RQ_FASYNC: 0 0 0 0 0.000 0.000
RQ_LOCK: 0 0 0 0 0.000 0.000
RQ_LINK: 0 0 0 0 0.000 0.000
RQ_SYMLINK: 0 0 0 0 0.000 0.000
RQ_MKDIR: 0 0 0 0 0.000 0.000
RQ_RMDIR: 0 0 0 0 0.000 0.000
RQ_MKNOD: 0 0 0 0 0.000 0.000
RQ_RENAME: 0 0 0 0 0.000 0.000
RQ_READLINK: 2 0 0 0 0.000 0.000
RQ_TRUNCATE: 0 0 0 0 0.000 0.000
RQ_SETATTR: 0 0 0 0 0.000 0.000
RQ_GETATTR: 72 0 0 0 0.000 0.000
RQ_PARALLEL_READ: 0 0 0 0 0.000 0.000
RQ_PARALLEL_WRITE: 0 0 0 0 0.000 0.000
RQ_STATFS: 21 0 0 0 0.000 0.000
RQ_READPAGE_ASYNC: 0 0 0 0 0.000 0.000
RQ_READPAGE_DATA: 0 0 0 0 0.000 0.000
RQ_GETEOI: 0 0 0 0 0.000 0.000
RQ_SETXATTR: 0 0 0 0 0.000 0.000
RQ_GETXATTR: 0 0 0 0 0.000 0.000
RQ_LISTXATTR: 0 0 0 0 0.000 0.000
RQ_REMOVEXATTR: 0 0 0 0 0.000 0.000
RQ_VERIFYFS: 0 0 0 0 0.000 0.000
RQ_RO_CACHE_DISABLE: 0 0 0 0 0.000 0.000
RQ_PERMISSION: 4 0 0 0 0.000 0.000
RQ_SYNC_UPDATE: 0 0 0 0 0.000 0.000
RQ_READPAGES_RQ: 29360 0 0 0 0.000 0.000
RQ_READPAGES_RP: 0 0 0 0 0.000 0.000
RQ_WRITEPAGES_RQ: 0 0 0 0 0.000 0.000
RQ_WRITEPAGES_RP: 0 0 0 0 0.000 0.000
llseek: 0 0 0.000 0.000
read: 0 0 0.000 0.000
aio_read: 951107 8 0.000 15.004
write: 0 0 0.000 0.000
aio_write: 0 0 0.000 0.000
readdir: 113550 0 0.000 0.796
unlocked_ioctl: 0 2 0.000 0.000
mmap: 0 0 0.000 0.000
open: 59362 0 0.000 0.012
flush: 228300 0 0.000 0.004
release: 59360 0 0.000 0.004
fsync: 0 0 0.000 0.000
fasync: 0 0 0.000 0.000
lock: 0 0 0.000 0.000
flock: 0 0 0.000 0.000
writepage: 0 0 0.000 0.000
writepages: 0 0 0.000 0.000
readpage: 160 0 0.000 0.004
readpages: 26627 0 0.000 0.744
write_begin: 0 0 0.000 0.000
write_end: 0 0 0.000 0.000
direct_io: 0 0 0.000 0.000
statfs: 21 0 0.000 0.004
put_super: 0 0 0.000 0.000
write_super: 0 0 0.000 0.000
evict_inode: 3 0 0.000 0.000
show_options: 81525 0 0.000 0.004
d_create: 0 0 0.000 0.000
d_lookup: 974281 0 0.000 0.016
d_link: 0 0 0.000 0.000
d_unlink: 0 0 0.000 0.000
d_symlink: 0 0 0.000 0.000
d_mkdir: 0 0 0.000 0.000
d_rmdir: 0 0 0.000 0.000
d_mknod: 0 0 0.000 0.000
d_rename: 0 0 0.000 0.000
d_truncate: 0 0 0.000 0.000
d_permission: 1149779 1090389 0.004 0.004
d_setattr: 0 0 0.000 0.000
d_getattr: 172814 0 0.004 0.004
d_setxattr: 0 0 0.000 0.000
d_getxattr: 0 0 0.000 0.000
d_listxattr: 0 0 0.000 0.000
d_removexattr: 0 0 0.000 0.000
f_create: 0 0 0.000 0.000
f_link: 0 0 0.000 0.000
f_unlink: 0 0 0.000 0.000
f_symlink: 0 0 0.000 0.000
f_mkdir: 0 0 0.000 0.000
f_rmdir: 0 0 0.000 0.000
f_mknod: 0 0 0.000 0.000
f_rename: 0 0 0.000 0.000
f_truncate: 0 0 0.000 0.000
f_permission: 2 0 0.000 0.000
f_setattr: 0 0 0.000 0.000
f_getattr: 858514 0 0.000 0.004
f_setxattr: 0 0 0.000 0.000
f_getxattr: 0 0 0.000 0.000
f_listxattr: 0 0 0.000 0.000
f_removexattr: 0 0 0.000 0.000
l_readlink: 0 0 0.000 0.000
l_follow_link: 0 0 0.000 0.000
l_put_link: 0 0 0.000 0.000
l_setattr: 0 0 0.000 0.000
l_getattr: 59088 0 0.000 0.004
d_revalidate: 113449 0 0.000 0.000
read_min_max: 0 0
write_min_max: 0 0
IPC requests: 0 0
IPC async requests: 0 0
IPC replies: 0 0
Open files: 2
Inodes created: 968132
Inodes removed: 4
"""),
        ],
        [
            LDMSData({
                "instance_name": LDMSD_PREFIX + "/var/opt/cray/imps",
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "mountpt": "/var/opt/cray/imps",
                    "RQ_LOOKUP_clnt_cnt": 974286,
                    "RQ_LOOKUP_clnt_err": 0,
                    "RQ_LOOKUP_srvr_cnt": 0,
                    "RQ_LOOKUP_srvr_err": 0,
                    "RQ_LOOKUP_last_dur": 0.000,
                    "RQ_LOOKUP_max_dur": 0.000,
                    "RQ_OPEN_clnt_cnt": 59362,
                    "RQ_OPEN_clnt_err": 0,
                    "RQ_OPEN_srvr_cnt": 0,
                    "RQ_OPEN_srvr_err": 0,
                    "RQ_OPEN_last_dur": 0.000,
                    "RQ_OPEN_max_dur": 0.000,
                    "RQ_CLOSE_clnt_cnt": 59360,
                    "RQ_CLOSE_clnt_err": 0,
                    "RQ_CLOSE_srvr_cnt": 0,
                    "RQ_CLOSE_srvr_err": 0,
                    "RQ_CLOSE_last_dur": 0.000,
                    "RQ_CLOSE_max_dur": 0.000,
                    "RQ_READDIR_clnt_cnt": 113550,
                    "RQ_READDIR_clnt_err": 0,
                    "RQ_READDIR_srvr_cnt": 0,
                    "RQ_READDIR_srvr_err": 0,
                    "RQ_READDIR_last_dur": 0.000,
                    "RQ_READDIR_max_dur": 0.000,
                    "RQ_CREATE_clnt_cnt": 0,
                    "RQ_CREATE_clnt_err": 0,
                    "RQ_CREATE_srvr_cnt": 0,
                    "RQ_CREATE_srvr_err": 0,
                    "RQ_CREATE_last_dur": 0.000,
                    "RQ_CREATE_max_dur": 0.000,
                    "RQ_UNLINK_clnt_cnt": 0,
                    "RQ_UNLINK_clnt_err": 0,
                    "RQ_UNLINK_srvr_cnt": 0,
                    "RQ_UNLINK_srvr_err": 0,
                    "RQ_UNLINK_last_dur": 0.000,
                    "RQ_UNLINK_max_dur": 0.000,
                    "RQ_IOCTL_clnt_cnt": 0,
                    "RQ_IOCTL_clnt_err": 0,
                    "RQ_IOCTL_srvr_cnt": 0,
                    "RQ_IOCTL_srvr_err": 0,
                    "RQ_IOCTL_last_dur": 0.000,
                    "RQ_IOCTL_max_dur": 0.000,
                    "RQ_FLUSH_clnt_cnt": 0,
                    "RQ_FLUSH_clnt_err": 0,
                    "RQ_FLUSH_srvr_cnt": 0,
                    "RQ_FLUSH_srvr_err": 0,
                    "RQ_FLUSH_last_dur": 0.000,
                    "RQ_FLUSH_max_dur": 0.000,
                    "RQ_FSYNC_clnt_cnt": 0,
                    "RQ_FSYNC_clnt_err": 0,
                    "RQ_FSYNC_srvr_cnt": 0,
                    "RQ_FSYNC_srvr_err": 0,
                    "RQ_FSYNC_last_dur": 0.000,
                    "RQ_FSYNC_max_dur": 0.000,
                    "RQ_FASYNC_clnt_cnt": 0,
                    "RQ_FASYNC_clnt_err": 0,
                    "RQ_FASYNC_srvr_cnt": 0,
                    "RQ_FASYNC_srvr_err": 0,
                    "RQ_FASYNC_last_dur": 0.000,
                    "RQ_FASYNC_max_dur": 0.000,
                    "RQ_LOCK_clnt_cnt": 0,
                    "RQ_LOCK_clnt_err": 0,
                    "RQ_LOCK_srvr_cnt": 0,
                    "RQ_LOCK_srvr_err": 0,
                    "RQ_LOCK_last_dur": 0.000,
                    "RQ_LOCK_max_dur": 0.000,
                    "RQ_LINK_clnt_cnt": 0,
                    "RQ_LINK_clnt_err": 0,
                    "RQ_LINK_srvr_cnt": 0,
                    "RQ_LINK_srvr_err": 0,
                    "RQ_LINK_last_dur": 0.000,
                    "RQ_LINK_max_dur": 0.000,
                    "RQ_SYMLINK_clnt_cnt": 0,
                    "RQ_SYMLINK_clnt_err": 0,
                    "RQ_SYMLINK_srvr_cnt": 0,
                    "RQ_SYMLINK_srvr_err": 0,
                    "RQ_SYMLINK_last_dur": 0.000,
                    "RQ_SYMLINK_max_dur": 0.000,
                    "RQ_MKDIR_clnt_cnt": 0,
                    "RQ_MKDIR_clnt_err": 0,
                    "RQ_MKDIR_srvr_cnt": 0,
                    "RQ_MKDIR_srvr_err": 0,
                    "RQ_MKDIR_last_dur": 0.000,
                    "RQ_MKDIR_max_dur": 0.000,
                    "RQ_RMDIR_clnt_cnt": 0,
                    "RQ_RMDIR_clnt_err": 0,
                    "RQ_RMDIR_srvr_cnt": 0,
                    "RQ_RMDIR_srvr_err": 0,
                    "RQ_RMDIR_last_dur": 0.000,
                    "RQ_RMDIR_max_dur": 0.000,
                    "RQ_MKNOD_clnt_cnt": 0,
                    "RQ_MKNOD_clnt_err": 0,
                    "RQ_MKNOD_srvr_cnt": 0,
                    "RQ_MKNOD_srvr_err": 0,
                    "RQ_MKNOD_last_dur": 0.000,
                    "RQ_MKNOD_max_dur": 0.000,
                    "RQ_RENAME_clnt_cnt": 0,
                    "RQ_RENAME_clnt_err": 0,
                    "RQ_RENAME_srvr_cnt": 0,
                    "RQ_RENAME_srvr_err": 0,
                    "RQ_RENAME_last_dur": 0.000,
                    "RQ_RENAME_max_dur": 0.000,
                    "RQ_READLINK_clnt_cnt": 2,
                    "RQ_READLINK_clnt_err": 0,
                    "RQ_READLINK_srvr_cnt": 0,
                    "RQ_READLINK_srvr_err": 0,
                    "RQ_READLINK_last_dur": 0.000,
                    "RQ_READLINK_max_dur": 0.000,
                    "RQ_TRUNCATE_clnt_cnt": 0,
                    "RQ_TRUNCATE_clnt_err": 0,
                    "RQ_TRUNCATE_srvr_cnt": 0,
                    "RQ_TRUNCATE_srvr_err": 0,
                    "RQ_TRUNCATE_last_dur": 0.000,
                    "RQ_TRUNCATE_max_dur": 0.000,
                    "RQ_SETATTR_clnt_cnt": 0,
                    "RQ_SETATTR_clnt_err": 0,
                    "RQ_SETATTR_srvr_cnt": 0,
                    "RQ_SETATTR_srvr_err": 0,
                    "RQ_SETATTR_last_dur": 0.000,
                    "RQ_SETATTR_max_dur": 0.000,
                    "RQ_GETATTR_clnt_cnt": 72,
                    "RQ_GETATTR_clnt_err": 0,
                    "RQ_GETATTR_srvr_cnt": 0,
                    "RQ_GETATTR_srvr_err": 0,
                    "RQ_GETATTR_last_dur": 0.000,
                    "RQ_GETATTR_max_dur": 0.000,
                    "RQ_PARALLEL_READ_clnt_cnt": 0,
                    "RQ_PARALLEL_READ_clnt_err": 0,
                    "RQ_PARALLEL_READ_srvr_cnt": 0,
                    "RQ_PARALLEL_READ_srvr_err": 0,
                    "RQ_PARALLEL_READ_last_dur": 0.000,
                    "RQ_PARALLEL_READ_max_dur": 0.000,
                    "RQ_PARALLEL_WRITE_clnt_cnt": 0,
                    "RQ_PARALLEL_WRITE_clnt_err": 0,
                    "RQ_PARALLEL_WRITE_srvr_cnt": 0,
                    "RQ_PARALLEL_WRITE_srvr_err": 0,
                    "RQ_PARALLEL_WRITE_last_dur": 0.000,
                    "RQ_PARALLEL_WRITE_max_dur": 0.000,
                    "RQ_STATFS_clnt_cnt": 21,
                    "RQ_STATFS_clnt_err": 0,
                    "RQ_STATFS_srvr_cnt": 0,
                    "RQ_STATFS_srvr_err": 0,
                    "RQ_STATFS_last_dur": 0.000,
                    "RQ_STATFS_max_dur": 0.000,
                    "RQ_READPAGE_ASYNC_clnt_cnt": 0,
                    "RQ_READPAGE_ASYNC_clnt_err": 0,
                    "RQ_READPAGE_ASYNC_srvr_cnt": 0,
                    "RQ_READPAGE_ASYNC_srvr_err": 0,
                    "RQ_READPAGE_ASYNC_last_dur": 0.000,
                    "RQ_READPAGE_ASYNC_max_dur": 0.000,
                    "RQ_READPAGE_DATA_clnt_cnt": 0,
                    "RQ_READPAGE_DATA_clnt_err": 0,
                    "RQ_READPAGE_DATA_srvr_cnt": 0,
                    "RQ_READPAGE_DATA_srvr_err": 0,
                    "RQ_READPAGE_DATA_last_dur": 0.000,
                    "RQ_READPAGE_DATA_max_dur": 0.000,
                    "RQ_GETEOI_clnt_cnt": 0,
                    "RQ_GETEOI_clnt_err": 0,
                    "RQ_GETEOI_srvr_cnt": 0,
                    "RQ_GETEOI_srvr_err": 0,
                    "RQ_GETEOI_last_dur": 0.000,
                    "RQ_GETEOI_max_dur": 0.000,
                    "RQ_SETXATTR_clnt_cnt": 0,
                    "RQ_SETXATTR_clnt_err": 0,
                    "RQ_SETXATTR_srvr_cnt": 0,
                    "RQ_SETXATTR_srvr_err": 0,
                    "RQ_SETXATTR_last_dur": 0.000,
                    "RQ_SETXATTR_max_dur": 0.000,
                    "RQ_GETXATTR_clnt_cnt": 0,
                    "RQ_GETXATTR_clnt_err": 0,
                    "RQ_GETXATTR_srvr_cnt": 0,
                    "RQ_GETXATTR_srvr_err": 0,
                    "RQ_GETXATTR_last_dur": 0.000,
                    "RQ_GETXATTR_max_dur": 0.000,
                    "RQ_LISTXATTR_clnt_cnt": 0,
                    "RQ_LISTXATTR_clnt_err": 0,
                    "RQ_LISTXATTR_srvr_cnt": 0,
                    "RQ_LISTXATTR_srvr_err": 0,
                    "RQ_LISTXATTR_last_dur": 0.000,
                    "RQ_LISTXATTR_max_dur": 0.000,
                    "RQ_REMOVEXATTR_clnt_cnt": 0,
                    "RQ_REMOVEXATTR_clnt_err": 0,
                    "RQ_REMOVEXATTR_srvr_cnt": 0,
                    "RQ_REMOVEXATTR_srvr_err": 0,
                    "RQ_REMOVEXATTR_last_dur": 0.000,
                    "RQ_REMOVEXATTR_max_dur": 0.000,
                    "RQ_VERIFYFS_clnt_cnt": 0,
                    "RQ_VERIFYFS_clnt_err": 0,
                    "RQ_VERIFYFS_srvr_cnt": 0,
                    "RQ_VERIFYFS_srvr_err": 0,
                    "RQ_VERIFYFS_last_dur": 0.000,
                    "RQ_VERIFYFS_max_dur": 0.000,
                    "RQ_RO_CACHE_DISABLE_clnt_cnt": 0,
                    "RQ_RO_CACHE_DISABLE_clnt_err": 0,
                    "RQ_RO_CACHE_DISABLE_srvr_cnt": 0,
                    "RQ_RO_CACHE_DISABLE_srvr_err": 0,
                    "RQ_RO_CACHE_DISABLE_last_dur": 0.000,
                    "RQ_RO_CACHE_DISABLE_max_dur": 0.000,
                    "RQ_PERMISSION_clnt_cnt": 4,
                    "RQ_PERMISSION_clnt_err": 0,
                    "RQ_PERMISSION_srvr_cnt": 0,
                    "RQ_PERMISSION_srvr_err": 0,
                    "RQ_PERMISSION_last_dur": 0.000,
                    "RQ_PERMISSION_max_dur": 0.000,
                    "RQ_SYNC_UPDATE_clnt_cnt": 0,
                    "RQ_SYNC_UPDATE_clnt_err": 0,
                    "RQ_SYNC_UPDATE_srvr_cnt": 0,
                    "RQ_SYNC_UPDATE_srvr_err": 0,
                    "RQ_SYNC_UPDATE_last_dur": 0.000,
                    "RQ_SYNC_UPDATE_max_dur": 0.000,
                    "RQ_READPAGES_RQ_clnt_cnt": 29360,
                    "RQ_READPAGES_RQ_clnt_err": 0,
                    "RQ_READPAGES_RQ_srvr_cnt": 0,
                    "RQ_READPAGES_RQ_srvr_err": 0,
                    "RQ_READPAGES_RQ_last_dur": 0.000,
                    "RQ_READPAGES_RQ_max_dur": 0.000,
                    "RQ_READPAGES_RP_clnt_cnt": 0,
                    "RQ_READPAGES_RP_clnt_err": 0,
                    "RQ_READPAGES_RP_srvr_cnt": 0,
                    "RQ_READPAGES_RP_srvr_err": 0,
                    "RQ_READPAGES_RP_last_dur": 0.000,
                    "RQ_READPAGES_RP_max_dur": 0.000,
                    "RQ_WRITEPAGES_RQ_clnt_cnt": 0,
                    "RQ_WRITEPAGES_RQ_clnt_err": 0,
                    "RQ_WRITEPAGES_RQ_srvr_cnt": 0,
                    "RQ_WRITEPAGES_RQ_srvr_err": 0,
                    "RQ_WRITEPAGES_RQ_last_dur": 0.000,
                    "RQ_WRITEPAGES_RQ_max_dur": 0.000,
                    "RQ_WRITEPAGES_RP_clnt_cnt": 0,
                    "RQ_WRITEPAGES_RP_clnt_err": 0,
                    "RQ_WRITEPAGES_RP_srvr_cnt": 0,
                    "RQ_WRITEPAGES_RP_srvr_err": 0,
                    "RQ_WRITEPAGES_RP_last_dur": 0.000,
                    "RQ_WRITEPAGES_RP_max_dur": 0.000,
                    "llseek_cnt": 0,
                    "llseek_err": 0,
                    "llseek_last_dur": 0.000,
                    "llseek_max_dur": 0.000,
                    "read_cnt": 0,
                    "read_err": 0,
                    "read_last_dur": 0.000,
                    "read_max_dur": 0.000,
                    "aio_read_cnt": 951107,
                    "aio_read_err": 8,
                    "aio_read_last_dur": 0.000,
                    "aio_read_max_dur": 15.004,
                    "write_cnt": 0,
                    "write_err": 0,
                    "write_last_dur": 0.000,
                    "write_max_dur": 0.000,
                    "aio_write_cnt": 0,
                    "aio_write_err": 0,
                    "aio_write_last_dur": 0.000,
                    "aio_write_max_dur": 0.000,
                    "readdir_cnt": 113550,
                    "readdir_err": 0,
                    "readdir_last_dur": 0.000,
                    "readdir_max_dur": 0.796,
                    "unlocked_ioctl_cnt": 0,
                    "unlocked_ioctl_err": 2,
                    "unlocked_ioctl_last_dur": 0.000,
                    "unlocked_ioctl_max_dur": 0.000,
                    "mmap_cnt": 0,
                    "mmap_err": 0,
                    "mmap_last_dur": 0.000,
                    "mmap_max_dur": 0.000,
                    "open_cnt": 59362,
                    "open_err": 0,
                    "open_last_dur": 0.000,
                    "open_max_dur": 0.012,
                    "flush_cnt": 228300,
                    "flush_err": 0,
                    "flush_last_dur": 0.000,
                    "flush_max_dur": 0.004,
                    "release_cnt": 59360,
                    "release_err": 0,
                    "release_last_dur": 0.000,
                    "release_max_dur": 0.004,
                    "fsync_cnt": 0,
                    "fsync_err": 0,
                    "fsync_last_dur": 0.000,
                    "fsync_max_dur": 0.000,
                    "fasync_cnt": 0,
                    "fasync_err": 0,
                    "fasync_last_dur": 0.000,
                    "fasync_max_dur": 0.000,
                    "lock_cnt": 0,
                    "lock_err": 0,
                    "lock_last_dur": 0.000,
                    "lock_max_dur": 0.000,
                    "flock_cnt": 0,
                    "flock_err": 0,
                    "flock_last_dur": 0.000,
                    "flock_max_dur": 0.000,
                    "writepage_cnt": 0,
                    "writepage_err": 0,
                    "writepage_last_dur": 0.000,
                    "writepage_max_dur": 0.000,
                    "writepages_cnt": 0,
                    "writepages_err": 0,
                    "writepages_last_dur": 0.000,
                    "writepages_max_dur": 0.000,
                    "readpage_cnt": 160,
                    "readpage_err": 0,
                    "readpage_last_dur": 0.000,
                    "readpage_max_dur": 0.004,
                    "readpages_cnt": 26627,
                    "readpages_err": 0,
                    "readpages_last_dur": 0.000,
                    "readpages_max_dur": 0.744,
                    "write_begin_cnt": 0,
                    "write_begin_err": 0,
                    "write_begin_last_dur": 0.000,
                    "write_begin_max_dur": 0.000,
                    "write_end_cnt": 0,
                    "write_end_err": 0,
                    "write_end_last_dur": 0.000,
                    "write_end_max_dur": 0.000,
                    "direct_io_cnt": 0,
                    "direct_io_err": 0,
                    "direct_io_last_dur": 0.000,
                    "direct_io_max_dur": 0.000,
                    "statfs_cnt": 21,
                    "statfs_err": 0,
                    "statfs_last_dur": 0.000,
                    "statfs_max_dur": 0.004,
                    "put_super_cnt": 0,
                    "put_super_err": 0,
                    "put_super_last_dur": 0.000,
                    "put_super_max_dur": 0.000,
                    "write_super_cnt": 0,
                    "write_super_err": 0,
                    "write_super_last_dur": 0.000,
                    "write_super_max_dur": 0.000,
                    "evict_inode_cnt": 3,
                    "evict_inode_err": 0,
                    "evict_inode_last_dur": 0.000,
                    "evict_inode_max_dur": 0.000,
                    "show_options_cnt": 81525,
                    "show_options_err": 0,
                    "show_options_last_dur": 0.000,
                    "show_options_max_dur": 0.004,
                    "d_create_cnt": 0,
                    "d_create_err": 0,
                    "d_create_last_dur": 0.000,
                    "d_create_max_dur": 0.000,
                    "d_lookup_cnt": 974281,
                    "d_lookup_err": 0,
                    "d_lookup_last_dur": 0.000,
                    "d_lookup_max_dur": 0.016,
                    "d_link_cnt": 0,
                    "d_link_err": 0,
                    "d_link_last_dur": 0.000,
                    "d_link_max_dur": 0.000,
                    "d_unlink_cnt": 0,
                    "d_unlink_err": 0,
                    "d_unlink_last_dur": 0.000,
                    "d_unlink_max_dur": 0.000,
                    "d_symlink_cnt": 0,
                    "d_symlink_err": 0,
                    "d_symlink_last_dur": 0.000,
                    "d_symlink_max_dur": 0.000,
                    "d_mkdir_cnt": 0,
                    "d_mkdir_err": 0,
                    "d_mkdir_last_dur": 0.000,
                    "d_mkdir_max_dur": 0.000,
                    "d_rmdir_cnt": 0,
                    "d_rmdir_err": 0,
                    "d_rmdir_last_dur": 0.000,
                    "d_rmdir_max_dur": 0.000,
                    "d_mknod_cnt": 0,
                    "d_mknod_err": 0,
                    "d_mknod_last_dur": 0.000,
                    "d_mknod_max_dur": 0.000,
                    "d_rename_cnt": 0,
                    "d_rename_err": 0,
                    "d_rename_last_dur": 0.000,
                    "d_rename_max_dur": 0.000,
                    "d_truncate_cnt": 0,
                    "d_truncate_err": 0,
                    "d_truncate_last_dur": 0.000,
                    "d_truncate_max_dur": 0.000,
                    "d_permission_cnt": 1149779,
                    "d_permission_err": 1090389,
                    "d_permission_last_dur": 0.004,
                    "d_permission_max_dur": 0.004,
                    "d_setattr_cnt": 0,
                    "d_setattr_err": 0,
                    "d_setattr_last_dur": 0.000,
                    "d_setattr_max_dur": 0.000,
                    "d_getattr_cnt": 172814,
                    "d_getattr_err": 0,
                    "d_getattr_last_dur": 0.004,
                    "d_getattr_max_dur": 0.004,
                    "d_setxattr_cnt": 0,
                    "d_setxattr_err": 0,
                    "d_setxattr_last_dur": 0.000,
                    "d_setxattr_max_dur": 0.000,
                    "d_getxattr_cnt": 0,
                    "d_getxattr_err": 0,
                    "d_getxattr_last_dur": 0.000,
                    "d_getxattr_max_dur": 0.000,
                    "d_listxattr_cnt": 0,
                    "d_listxattr_err": 0,
                    "d_listxattr_last_dur": 0.000,
                    "d_listxattr_max_dur": 0.000,
                    "d_removexattr_cnt": 0,
                    "d_removexattr_err": 0,
                    "d_removexattr_last_dur": 0.000,
                    "d_removexattr_max_dur": 0.000,
                    "f_create_cnt": 0,
                    "f_create_err": 0,
                    "f_create_last_dur": 0.000,
                    "f_create_max_dur": 0.000,
                    "f_link_cnt": 0,
                    "f_link_err": 0,
                    "f_link_last_dur": 0.000,
                    "f_link_max_dur": 0.000,
                    "f_unlink_cnt": 0,
                    "f_unlink_err": 0,
                    "f_unlink_last_dur": 0.000,
                    "f_unlink_max_dur": 0.000,
                    "f_symlink_cnt": 0,
                    "f_symlink_err": 0,
                    "f_symlink_last_dur": 0.000,
                    "f_symlink_max_dur": 0.000,
                    "f_mkdir_cnt": 0,
                    "f_mkdir_err": 0,
                    "f_mkdir_last_dur": 0.000,
                    "f_mkdir_max_dur": 0.000,
                    "f_rmdir_cnt": 0,
                    "f_rmdir_err": 0,
                    "f_rmdir_last_dur": 0.000,
                    "f_rmdir_max_dur": 0.000,
                    "f_mknod_cnt": 0,
                    "f_mknod_err": 0,
                    "f_mknod_last_dur": 0.000,
                    "f_mknod_max_dur": 0.000,
                    "f_rename_cnt": 0,
                    "f_rename_err": 0,
                    "f_rename_last_dur": 0.000,
                    "f_rename_max_dur": 0.000,
                    "f_truncate_cnt": 0,
                    "f_truncate_err": 0,
                    "f_truncate_last_dur": 0.000,
                    "f_truncate_max_dur": 0.000,
                    "f_permission_cnt": 2,
                    "f_permission_err": 0,
                    "f_permission_last_dur": 0.000,
                    "f_permission_max_dur": 0.000,
                    "f_setattr_cnt": 0,
                    "f_setattr_err": 0,
                    "f_setattr_last_dur": 0.000,
                    "f_setattr_max_dur": 0.000,
                    "f_getattr_cnt": 858514,
                    "f_getattr_err": 0,
                    "f_getattr_last_dur": 0.000,
                    "f_getattr_max_dur": 0.004,
                    "f_setxattr_cnt": 0,
                    "f_setxattr_err": 0,
                    "f_setxattr_last_dur": 0.000,
                    "f_setxattr_max_dur": 0.000,
                    "f_getxattr_cnt": 0,
                    "f_getxattr_err": 0,
                    "f_getxattr_last_dur": 0.000,
                    "f_getxattr_max_dur": 0.000,
                    "f_listxattr_cnt": 0,
                    "f_listxattr_err": 0,
                    "f_listxattr_last_dur": 0.000,
                    "f_listxattr_max_dur": 0.000,
                    "f_removexattr_cnt": 0,
                    "f_removexattr_err": 0,
                    "f_removexattr_last_dur": 0.000,
                    "f_removexattr_max_dur": 0.000,
                    "l_readlink_cnt": 0,
                    "l_readlink_err": 0,
                    "l_readlink_last_dur": 0.000,
                    "l_readlink_max_dur": 0.000,
                    "l_follow_link_cnt": 0,
                    "l_follow_link_err": 0,
                    "l_follow_link_last_dur": 0.000,
                    "l_follow_link_max_dur": 0.000,
                    "l_put_link_cnt": 0,
                    "l_put_link_err": 0,
                    "l_put_link_last_dur": 0.000,
                    "l_put_link_max_dur": 0.000,
                    "l_setattr_cnt": 0,
                    "l_setattr_err": 0,
                    "l_setattr_last_dur": 0.000,
                    "l_setattr_max_dur": 0.000,
                    "l_getattr_cnt": 59088,
                    "l_getattr_err": 0,
                    "l_getattr_last_dur": 0.000,
                    "l_getattr_max_dur": 0.004,
                    "d_revalidate_cnt": 113449,
                    "d_revalidate_err": 0,
                    "d_revalidate_last_dur": 0.000,
                    "d_revalidate_max_dur": 0.000,
                    "read_min_max_min": 0,
                    "read_min_max_max": 0,
                    "write_min_max_min": 0,
                    "write_min_max_max": 0,
                    "IPC_requests_cnt": 0,
                    "IPC_requests_err": 0,
                    "IPC_async_requests_cnt": 0,
                    "IPC_async_requests_err": 0,
                    "IPC_replies_cnt": 0,
                    "IPC_replies_err": 0,
                    "Open_files": 2,
                    "Inodes_created": 968132,
                    "Inodes_removed": 3,
                }
            }),
            LDMSData({
                "instance_name": LDMSD_PREFIX + "/test/path",
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "mountpt": "/test/path",
                    "RQ_LOOKUP_clnt_cnt": 974286,
                    "RQ_LOOKUP_clnt_err": 0,
                    "RQ_LOOKUP_srvr_cnt": 0,
                    "RQ_LOOKUP_srvr_err": 0,
                    "RQ_LOOKUP_last_dur": 0.000,
                    "RQ_LOOKUP_max_dur": 0.000,
                    "RQ_OPEN_clnt_cnt": 59362,
                    "RQ_OPEN_clnt_err": 0,
                    "RQ_OPEN_srvr_cnt": 0,
                    "RQ_OPEN_srvr_err": 0,
                    "RQ_OPEN_last_dur": 0.000,
                    "RQ_OPEN_max_dur": 0.000,
                    "RQ_CLOSE_clnt_cnt": 59360,
                    "RQ_CLOSE_clnt_err": 0,
                    "RQ_CLOSE_srvr_cnt": 0,
                    "RQ_CLOSE_srvr_err": 0,
                    "RQ_CLOSE_last_dur": 0.000,
                    "RQ_CLOSE_max_dur": 0.000,
                    "RQ_READDIR_clnt_cnt": 113550,
                    "RQ_READDIR_clnt_err": 0,
                    "RQ_READDIR_srvr_cnt": 0,
                    "RQ_READDIR_srvr_err": 0,
                    "RQ_READDIR_last_dur": 0.000,
                    "RQ_READDIR_max_dur": 0.000,
                    "RQ_CREATE_clnt_cnt": 0,
                    "RQ_CREATE_clnt_err": 0,
                    "RQ_CREATE_srvr_cnt": 0,
                    "RQ_CREATE_srvr_err": 0,
                    "RQ_CREATE_last_dur": 0.000,
                    "RQ_CREATE_max_dur": 0.000,
                    "RQ_UNLINK_clnt_cnt": 0,
                    "RQ_UNLINK_clnt_err": 0,
                    "RQ_UNLINK_srvr_cnt": 0,
                    "RQ_UNLINK_srvr_err": 0,
                    "RQ_UNLINK_last_dur": 0.000,
                    "RQ_UNLINK_max_dur": 0.000,
                    "RQ_IOCTL_clnt_cnt": 0,
                    "RQ_IOCTL_clnt_err": 0,
                    "RQ_IOCTL_srvr_cnt": 0,
                    "RQ_IOCTL_srvr_err": 0,
                    "RQ_IOCTL_last_dur": 0.000,
                    "RQ_IOCTL_max_dur": 0.000,
                    "RQ_FLUSH_clnt_cnt": 0,
                    "RQ_FLUSH_clnt_err": 0,
                    "RQ_FLUSH_srvr_cnt": 0,
                    "RQ_FLUSH_srvr_err": 0,
                    "RQ_FLUSH_last_dur": 0.000,
                    "RQ_FLUSH_max_dur": 0.000,
                    "RQ_FSYNC_clnt_cnt": 0,
                    "RQ_FSYNC_clnt_err": 0,
                    "RQ_FSYNC_srvr_cnt": 0,
                    "RQ_FSYNC_srvr_err": 0,
                    "RQ_FSYNC_last_dur": 0.000,
                    "RQ_FSYNC_max_dur": 0.000,
                    "RQ_FASYNC_clnt_cnt": 0,
                    "RQ_FASYNC_clnt_err": 0,
                    "RQ_FASYNC_srvr_cnt": 0,
                    "RQ_FASYNC_srvr_err": 0,
                    "RQ_FASYNC_last_dur": 0.000,
                    "RQ_FASYNC_max_dur": 0.000,
                    "RQ_LOCK_clnt_cnt": 0,
                    "RQ_LOCK_clnt_err": 0,
                    "RQ_LOCK_srvr_cnt": 0,
                    "RQ_LOCK_srvr_err": 0,
                    "RQ_LOCK_last_dur": 0.000,
                    "RQ_LOCK_max_dur": 0.000,
                    "RQ_LINK_clnt_cnt": 0,
                    "RQ_LINK_clnt_err": 0,
                    "RQ_LINK_srvr_cnt": 0,
                    "RQ_LINK_srvr_err": 0,
                    "RQ_LINK_last_dur": 0.000,
                    "RQ_LINK_max_dur": 0.000,
                    "RQ_SYMLINK_clnt_cnt": 0,
                    "RQ_SYMLINK_clnt_err": 0,
                    "RQ_SYMLINK_srvr_cnt": 0,
                    "RQ_SYMLINK_srvr_err": 0,
                    "RQ_SYMLINK_last_dur": 0.000,
                    "RQ_SYMLINK_max_dur": 0.000,
                    "RQ_MKDIR_clnt_cnt": 0,
                    "RQ_MKDIR_clnt_err": 0,
                    "RQ_MKDIR_srvr_cnt": 0,
                    "RQ_MKDIR_srvr_err": 0,
                    "RQ_MKDIR_last_dur": 0.000,
                    "RQ_MKDIR_max_dur": 0.000,
                    "RQ_RMDIR_clnt_cnt": 0,
                    "RQ_RMDIR_clnt_err": 0,
                    "RQ_RMDIR_srvr_cnt": 0,
                    "RQ_RMDIR_srvr_err": 0,
                    "RQ_RMDIR_last_dur": 0.000,
                    "RQ_RMDIR_max_dur": 0.000,
                    "RQ_MKNOD_clnt_cnt": 0,
                    "RQ_MKNOD_clnt_err": 0,
                    "RQ_MKNOD_srvr_cnt": 0,
                    "RQ_MKNOD_srvr_err": 0,
                    "RQ_MKNOD_last_dur": 0.000,
                    "RQ_MKNOD_max_dur": 0.000,
                    "RQ_RENAME_clnt_cnt": 0,
                    "RQ_RENAME_clnt_err": 0,
                    "RQ_RENAME_srvr_cnt": 0,
                    "RQ_RENAME_srvr_err": 0,
                    "RQ_RENAME_last_dur": 0.000,
                    "RQ_RENAME_max_dur": 0.000,
                    "RQ_READLINK_clnt_cnt": 2,
                    "RQ_READLINK_clnt_err": 0,
                    "RQ_READLINK_srvr_cnt": 0,
                    "RQ_READLINK_srvr_err": 0,
                    "RQ_READLINK_last_dur": 0.000,
                    "RQ_READLINK_max_dur": 0.000,
                    "RQ_TRUNCATE_clnt_cnt": 0,
                    "RQ_TRUNCATE_clnt_err": 0,
                    "RQ_TRUNCATE_srvr_cnt": 0,
                    "RQ_TRUNCATE_srvr_err": 0,
                    "RQ_TRUNCATE_last_dur": 0.000,
                    "RQ_TRUNCATE_max_dur": 0.000,
                    "RQ_SETATTR_clnt_cnt": 0,
                    "RQ_SETATTR_clnt_err": 0,
                    "RQ_SETATTR_srvr_cnt": 0,
                    "RQ_SETATTR_srvr_err": 0,
                    "RQ_SETATTR_last_dur": 0.000,
                    "RQ_SETATTR_max_dur": 0.000,
                    "RQ_GETATTR_clnt_cnt": 72,
                    "RQ_GETATTR_clnt_err": 0,
                    "RQ_GETATTR_srvr_cnt": 0,
                    "RQ_GETATTR_srvr_err": 0,
                    "RQ_GETATTR_last_dur": 0.000,
                    "RQ_GETATTR_max_dur": 0.000,
                    "RQ_PARALLEL_READ_clnt_cnt": 0,
                    "RQ_PARALLEL_READ_clnt_err": 0,
                    "RQ_PARALLEL_READ_srvr_cnt": 0,
                    "RQ_PARALLEL_READ_srvr_err": 0,
                    "RQ_PARALLEL_READ_last_dur": 0.000,
                    "RQ_PARALLEL_READ_max_dur": 0.000,
                    "RQ_PARALLEL_WRITE_clnt_cnt": 0,
                    "RQ_PARALLEL_WRITE_clnt_err": 0,
                    "RQ_PARALLEL_WRITE_srvr_cnt": 0,
                    "RQ_PARALLEL_WRITE_srvr_err": 0,
                    "RQ_PARALLEL_WRITE_last_dur": 0.000,
                    "RQ_PARALLEL_WRITE_max_dur": 0.000,
                    "RQ_STATFS_clnt_cnt": 21,
                    "RQ_STATFS_clnt_err": 0,
                    "RQ_STATFS_srvr_cnt": 0,
                    "RQ_STATFS_srvr_err": 0,
                    "RQ_STATFS_last_dur": 0.000,
                    "RQ_STATFS_max_dur": 0.000,
                    "RQ_READPAGE_ASYNC_clnt_cnt": 0,
                    "RQ_READPAGE_ASYNC_clnt_err": 0,
                    "RQ_READPAGE_ASYNC_srvr_cnt": 0,
                    "RQ_READPAGE_ASYNC_srvr_err": 0,
                    "RQ_READPAGE_ASYNC_last_dur": 0.000,
                    "RQ_READPAGE_ASYNC_max_dur": 0.000,
                    "RQ_READPAGE_DATA_clnt_cnt": 0,
                    "RQ_READPAGE_DATA_clnt_err": 0,
                    "RQ_READPAGE_DATA_srvr_cnt": 0,
                    "RQ_READPAGE_DATA_srvr_err": 0,
                    "RQ_READPAGE_DATA_last_dur": 0.000,
                    "RQ_READPAGE_DATA_max_dur": 0.000,
                    "RQ_GETEOI_clnt_cnt": 0,
                    "RQ_GETEOI_clnt_err": 0,
                    "RQ_GETEOI_srvr_cnt": 0,
                    "RQ_GETEOI_srvr_err": 0,
                    "RQ_GETEOI_last_dur": 0.000,
                    "RQ_GETEOI_max_dur": 0.000,
                    "RQ_SETXATTR_clnt_cnt": 0,
                    "RQ_SETXATTR_clnt_err": 0,
                    "RQ_SETXATTR_srvr_cnt": 0,
                    "RQ_SETXATTR_srvr_err": 0,
                    "RQ_SETXATTR_last_dur": 0.000,
                    "RQ_SETXATTR_max_dur": 0.000,
                    "RQ_GETXATTR_clnt_cnt": 0,
                    "RQ_GETXATTR_clnt_err": 0,
                    "RQ_GETXATTR_srvr_cnt": 0,
                    "RQ_GETXATTR_srvr_err": 0,
                    "RQ_GETXATTR_last_dur": 0.000,
                    "RQ_GETXATTR_max_dur": 0.000,
                    "RQ_LISTXATTR_clnt_cnt": 0,
                    "RQ_LISTXATTR_clnt_err": 0,
                    "RQ_LISTXATTR_srvr_cnt": 0,
                    "RQ_LISTXATTR_srvr_err": 0,
                    "RQ_LISTXATTR_last_dur": 0.000,
                    "RQ_LISTXATTR_max_dur": 0.000,
                    "RQ_REMOVEXATTR_clnt_cnt": 0,
                    "RQ_REMOVEXATTR_clnt_err": 0,
                    "RQ_REMOVEXATTR_srvr_cnt": 0,
                    "RQ_REMOVEXATTR_srvr_err": 0,
                    "RQ_REMOVEXATTR_last_dur": 0.000,
                    "RQ_REMOVEXATTR_max_dur": 0.000,
                    "RQ_VERIFYFS_clnt_cnt": 0,
                    "RQ_VERIFYFS_clnt_err": 0,
                    "RQ_VERIFYFS_srvr_cnt": 0,
                    "RQ_VERIFYFS_srvr_err": 0,
                    "RQ_VERIFYFS_last_dur": 0.000,
                    "RQ_VERIFYFS_max_dur": 0.000,
                    "RQ_RO_CACHE_DISABLE_clnt_cnt": 0,
                    "RQ_RO_CACHE_DISABLE_clnt_err": 0,
                    "RQ_RO_CACHE_DISABLE_srvr_cnt": 0,
                    "RQ_RO_CACHE_DISABLE_srvr_err": 0,
                    "RQ_RO_CACHE_DISABLE_last_dur": 0.000,
                    "RQ_RO_CACHE_DISABLE_max_dur": 0.000,
                    "RQ_PERMISSION_clnt_cnt": 4,
                    "RQ_PERMISSION_clnt_err": 0,
                    "RQ_PERMISSION_srvr_cnt": 0,
                    "RQ_PERMISSION_srvr_err": 0,
                    "RQ_PERMISSION_last_dur": 0.000,
                    "RQ_PERMISSION_max_dur": 0.000,
                    "RQ_SYNC_UPDATE_clnt_cnt": 0,
                    "RQ_SYNC_UPDATE_clnt_err": 0,
                    "RQ_SYNC_UPDATE_srvr_cnt": 0,
                    "RQ_SYNC_UPDATE_srvr_err": 0,
                    "RQ_SYNC_UPDATE_last_dur": 0.000,
                    "RQ_SYNC_UPDATE_max_dur": 0.000,
                    "RQ_READPAGES_RQ_clnt_cnt": 29360,
                    "RQ_READPAGES_RQ_clnt_err": 0,
                    "RQ_READPAGES_RQ_srvr_cnt": 0,
                    "RQ_READPAGES_RQ_srvr_err": 0,
                    "RQ_READPAGES_RQ_last_dur": 0.000,
                    "RQ_READPAGES_RQ_max_dur": 0.000,
                    "RQ_READPAGES_RP_clnt_cnt": 0,
                    "RQ_READPAGES_RP_clnt_err": 0,
                    "RQ_READPAGES_RP_srvr_cnt": 0,
                    "RQ_READPAGES_RP_srvr_err": 0,
                    "RQ_READPAGES_RP_last_dur": 0.000,
                    "RQ_READPAGES_RP_max_dur": 0.000,
                    "RQ_WRITEPAGES_RQ_clnt_cnt": 0,
                    "RQ_WRITEPAGES_RQ_clnt_err": 0,
                    "RQ_WRITEPAGES_RQ_srvr_cnt": 0,
                    "RQ_WRITEPAGES_RQ_srvr_err": 0,
                    "RQ_WRITEPAGES_RQ_last_dur": 0.000,
                    "RQ_WRITEPAGES_RQ_max_dur": 0.000,
                    "RQ_WRITEPAGES_RP_clnt_cnt": 0,
                    "RQ_WRITEPAGES_RP_clnt_err": 0,
                    "RQ_WRITEPAGES_RP_srvr_cnt": 0,
                    "RQ_WRITEPAGES_RP_srvr_err": 0,
                    "RQ_WRITEPAGES_RP_last_dur": 0.000,
                    "RQ_WRITEPAGES_RP_max_dur": 0.000,
                    "llseek_cnt": 0,
                    "llseek_err": 0,
                    "llseek_last_dur": 0.000,
                    "llseek_max_dur": 0.000,
                    "read_cnt": 0,
                    "read_err": 0,
                    "read_last_dur": 0.000,
                    "read_max_dur": 0.000,
                    "aio_read_cnt": 951107,
                    "aio_read_err": 8,
                    "aio_read_last_dur": 0.000,
                    "aio_read_max_dur": 15.004,
                    "write_cnt": 0,
                    "write_err": 0,
                    "write_last_dur": 0.000,
                    "write_max_dur": 0.000,
                    "aio_write_cnt": 0,
                    "aio_write_err": 0,
                    "aio_write_last_dur": 0.000,
                    "aio_write_max_dur": 0.000,
                    "readdir_cnt": 113550,
                    "readdir_err": 0,
                    "readdir_last_dur": 0.000,
                    "readdir_max_dur": 0.796,
                    "unlocked_ioctl_cnt": 0,
                    "unlocked_ioctl_err": 2,
                    "unlocked_ioctl_last_dur": 0.000,
                    "unlocked_ioctl_max_dur": 0.000,
                    "mmap_cnt": 0,
                    "mmap_err": 0,
                    "mmap_last_dur": 0.000,
                    "mmap_max_dur": 0.000,
                    "open_cnt": 59362,
                    "open_err": 0,
                    "open_last_dur": 0.000,
                    "open_max_dur": 0.012,
                    "flush_cnt": 228300,
                    "flush_err": 0,
                    "flush_last_dur": 0.000,
                    "flush_max_dur": 0.004,
                    "release_cnt": 59360,
                    "release_err": 0,
                    "release_last_dur": 0.000,
                    "release_max_dur": 0.004,
                    "fsync_cnt": 0,
                    "fsync_err": 0,
                    "fsync_last_dur": 0.000,
                    "fsync_max_dur": 0.000,
                    "fasync_cnt": 0,
                    "fasync_err": 0,
                    "fasync_last_dur": 0.000,
                    "fasync_max_dur": 0.000,
                    "lock_cnt": 0,
                    "lock_err": 0,
                    "lock_last_dur": 0.000,
                    "lock_max_dur": 0.000,
                    "flock_cnt": 0,
                    "flock_err": 0,
                    "flock_last_dur": 0.000,
                    "flock_max_dur": 0.000,
                    "writepage_cnt": 0,
                    "writepage_err": 0,
                    "writepage_last_dur": 0.000,
                    "writepage_max_dur": 0.000,
                    "writepages_cnt": 0,
                    "writepages_err": 0,
                    "writepages_last_dur": 0.000,
                    "writepages_max_dur": 0.000,
                    "readpage_cnt": 160,
                    "readpage_err": 0,
                    "readpage_last_dur": 0.000,
                    "readpage_max_dur": 0.004,
                    "readpages_cnt": 26627,
                    "readpages_err": 0,
                    "readpages_last_dur": 0.000,
                    "readpages_max_dur": 0.744,
                    "write_begin_cnt": 0,
                    "write_begin_err": 0,
                    "write_begin_last_dur": 0.000,
                    "write_begin_max_dur": 0.000,
                    "write_end_cnt": 0,
                    "write_end_err": 0,
                    "write_end_last_dur": 0.000,
                    "write_end_max_dur": 0.000,
                    "direct_io_cnt": 0,
                    "direct_io_err": 0,
                    "direct_io_last_dur": 0.000,
                    "direct_io_max_dur": 0.000,
                    "statfs_cnt": 21,
                    "statfs_err": 0,
                    "statfs_last_dur": 0.000,
                    "statfs_max_dur": 0.004,
                    "put_super_cnt": 0,
                    "put_super_err": 0,
                    "put_super_last_dur": 0.000,
                    "put_super_max_dur": 0.000,
                    "write_super_cnt": 0,
                    "write_super_err": 0,
                    "write_super_last_dur": 0.000,
                    "write_super_max_dur": 0.000,
                    "evict_inode_cnt": 3,
                    "evict_inode_err": 0,
                    "evict_inode_last_dur": 0.000,
                    "evict_inode_max_dur": 0.000,
                    "show_options_cnt": 81525,
                    "show_options_err": 0,
                    "show_options_last_dur": 0.000,
                    "show_options_max_dur": 0.004,
                    "d_create_cnt": 0,
                    "d_create_err": 0,
                    "d_create_last_dur": 0.000,
                    "d_create_max_dur": 0.000,
                    "d_lookup_cnt": 974281,
                    "d_lookup_err": 0,
                    "d_lookup_last_dur": 0.000,
                    "d_lookup_max_dur": 0.016,
                    "d_link_cnt": 0,
                    "d_link_err": 0,
                    "d_link_last_dur": 0.000,
                    "d_link_max_dur": 0.000,
                    "d_unlink_cnt": 0,
                    "d_unlink_err": 0,
                    "d_unlink_last_dur": 0.000,
                    "d_unlink_max_dur": 0.000,
                    "d_symlink_cnt": 0,
                    "d_symlink_err": 0,
                    "d_symlink_last_dur": 0.000,
                    "d_symlink_max_dur": 0.000,
                    "d_mkdir_cnt": 0,
                    "d_mkdir_err": 0,
                    "d_mkdir_last_dur": 0.000,
                    "d_mkdir_max_dur": 0.000,
                    "d_rmdir_cnt": 0,
                    "d_rmdir_err": 0,
                    "d_rmdir_last_dur": 0.000,
                    "d_rmdir_max_dur": 0.000,
                    "d_mknod_cnt": 0,
                    "d_mknod_err": 0,
                    "d_mknod_last_dur": 0.000,
                    "d_mknod_max_dur": 0.000,
                    "d_rename_cnt": 0,
                    "d_rename_err": 0,
                    "d_rename_last_dur": 0.000,
                    "d_rename_max_dur": 0.000,
                    "d_truncate_cnt": 0,
                    "d_truncate_err": 0,
                    "d_truncate_last_dur": 0.000,
                    "d_truncate_max_dur": 0.000,
                    "d_permission_cnt": 1149779,
                    "d_permission_err": 1090389,
                    "d_permission_last_dur": 0.004,
                    "d_permission_max_dur": 0.004,
                    "d_setattr_cnt": 0,
                    "d_setattr_err": 0,
                    "d_setattr_last_dur": 0.000,
                    "d_setattr_max_dur": 0.000,
                    "d_getattr_cnt": 172814,
                    "d_getattr_err": 0,
                    "d_getattr_last_dur": 0.004,
                    "d_getattr_max_dur": 0.004,
                    "d_setxattr_cnt": 0,
                    "d_setxattr_err": 0,
                    "d_setxattr_last_dur": 0.000,
                    "d_setxattr_max_dur": 0.000,
                    "d_getxattr_cnt": 0,
                    "d_getxattr_err": 0,
                    "d_getxattr_last_dur": 0.000,
                    "d_getxattr_max_dur": 0.000,
                    "d_listxattr_cnt": 0,
                    "d_listxattr_err": 0,
                    "d_listxattr_last_dur": 0.000,
                    "d_listxattr_max_dur": 0.000,
                    "d_removexattr_cnt": 0,
                    "d_removexattr_err": 0,
                    "d_removexattr_last_dur": 0.000,
                    "d_removexattr_max_dur": 0.000,
                    "f_create_cnt": 0,
                    "f_create_err": 0,
                    "f_create_last_dur": 0.000,
                    "f_create_max_dur": 0.000,
                    "f_link_cnt": 0,
                    "f_link_err": 0,
                    "f_link_last_dur": 0.000,
                    "f_link_max_dur": 0.000,
                    "f_unlink_cnt": 0,
                    "f_unlink_err": 0,
                    "f_unlink_last_dur": 0.000,
                    "f_unlink_max_dur": 0.000,
                    "f_symlink_cnt": 0,
                    "f_symlink_err": 0,
                    "f_symlink_last_dur": 0.000,
                    "f_symlink_max_dur": 0.000,
                    "f_mkdir_cnt": 0,
                    "f_mkdir_err": 0,
                    "f_mkdir_last_dur": 0.000,
                    "f_mkdir_max_dur": 0.000,
                    "f_rmdir_cnt": 0,
                    "f_rmdir_err": 0,
                    "f_rmdir_last_dur": 0.000,
                    "f_rmdir_max_dur": 0.000,
                    "f_mknod_cnt": 0,
                    "f_mknod_err": 0,
                    "f_mknod_last_dur": 0.000,
                    "f_mknod_max_dur": 0.000,
                    "f_rename_cnt": 0,
                    "f_rename_err": 0,
                    "f_rename_last_dur": 0.000,
                    "f_rename_max_dur": 0.000,
                    "f_truncate_cnt": 0,
                    "f_truncate_err": 0,
                    "f_truncate_last_dur": 0.000,
                    "f_truncate_max_dur": 0.000,
                    "f_permission_cnt": 2,
                    "f_permission_err": 0,
                    "f_permission_last_dur": 0.000,
                    "f_permission_max_dur": 0.000,
                    "f_setattr_cnt": 0,
                    "f_setattr_err": 0,
                    "f_setattr_last_dur": 0.000,
                    "f_setattr_max_dur": 0.000,
                    "f_getattr_cnt": 858514,
                    "f_getattr_err": 0,
                    "f_getattr_last_dur": 0.000,
                    "f_getattr_max_dur": 0.004,
                    "f_setxattr_cnt": 0,
                    "f_setxattr_err": 0,
                    "f_setxattr_last_dur": 0.000,
                    "f_setxattr_max_dur": 0.000,
                    "f_getxattr_cnt": 0,
                    "f_getxattr_err": 0,
                    "f_getxattr_last_dur": 0.000,
                    "f_getxattr_max_dur": 0.000,
                    "f_listxattr_cnt": 0,
                    "f_listxattr_err": 0,
                    "f_listxattr_last_dur": 0.000,
                    "f_listxattr_max_dur": 0.000,
                    "f_removexattr_cnt": 0,
                    "f_removexattr_err": 0,
                    "f_removexattr_last_dur": 0.000,
                    "f_removexattr_max_dur": 0.000,
                    "l_readlink_cnt": 0,
                    "l_readlink_err": 0,
                    "l_readlink_last_dur": 0.000,
                    "l_readlink_max_dur": 0.000,
                    "l_follow_link_cnt": 0,
                    "l_follow_link_err": 0,
                    "l_follow_link_last_dur": 0.000,
                    "l_follow_link_max_dur": 0.000,
                    "l_put_link_cnt": 0,
                    "l_put_link_err": 0,
                    "l_put_link_last_dur": 0.000,
                    "l_put_link_max_dur": 0.000,
                    "l_setattr_cnt": 0,
                    "l_setattr_err": 0,
                    "l_setattr_last_dur": 0.000,
                    "l_setattr_max_dur": 0.000,
                    "l_getattr_cnt": 59088,
                    "l_getattr_err": 0,
                    "l_getattr_last_dur": 0.000,
                    "l_getattr_max_dur": 0.004,
                    "d_revalidate_cnt": 113449,
                    "d_revalidate_err": 0,
                    "d_revalidate_last_dur": 0.000,
                    "d_revalidate_max_dur": 0.000,
                    "read_min_max_min": 0,
                    "read_min_max_max": 0,
                    "write_min_max_min": 0,
                    "write_min_max_max": 0,
                    "IPC_requests_cnt": 0,
                    "IPC_requests_err": 0,
                    "IPC_async_requests_cnt": 0,
                    "IPC_async_requests_err": 0,
                    "IPC_replies_cnt": 0,
                    "IPC_replies_err": 0,
                    "Open_files": 2,
                    "Inodes_created": 968132,
                    "Inodes_removed": 4,
                }
            }),
        ]
    ),
    SrcData([
            Src("/proc/fs/dvs/mounts/0/mount",
"""\
local-mount /var/opt/cray/imps
remote-path /var/opt/cray/imps
options (ro,blksize=524288,statsfile=/proc/fs/dvs/mounts/1/stats,attrcache_timeout=14400,nodwfs,nodwcfs,noparallelwrite,nomultifsync,cache,nodatasync,noclosesync,retry,failover,userenv,clusterfs,killprocess,noatomic,nodeferopens,no_distribute_create_ops,no_ro_cache,loadbalance,maxnodes=1,nnodes=1,nomagic,nohash_on_nid,hash=modulo,nodefile=/proc/fs/dvs/mounts/1/nodenames,nodename=c0-0c0s0n2)
active_nodes c0-0c0s0n2
inactive_nodes
loadbalance_node c0-0c0s0n2
remote-magic 0x6969
"""),
            Src("/proc/fs/dvs/mounts/1/mount",
"""\
local-mount /test/path
remote-path /test/path
options (ro,blksize=524288,statsfile=/proc/fs/dvs/mounts/1/stats,attrcache_timeout=14400,nodwfs,nodwcfs,noparallelwrite,nomultifsync,cache,nodatasync,noclosesync,retry,failover,userenv,clusterfs,killprocess,noatomic,nodeferopens,no_distribute_create_ops,no_ro_cache,loadbalance,maxnodes=1,nnodes=1,nomagic,nohash_on_nid,hash=modulo,nodefile=/proc/fs/dvs/mounts/1/nodenames,nodename=c0-0c0s0n2)
active_nodes c0-0c0s0n2
inactive_nodes
loadbalance_node c0-0c0s0n2
remote-magic 0x6969
"""),
            Src("/proc/fs/dvs/mounts/0/stats",
"""\
RQ_LOOKUP: 974286 0 0 0 0.000 0.000
RQ_OPEN: 59362 0 0 0 0.000 0.000
RQ_CLOSE: 59360 0 0 0 0.000 0.000
RQ_READDIR: 113550 0 0 0 0.000 0.000
RQ_CREATE: 0 0 0 0 0.000 0.000
RQ_UNLINK: 0 0 0 0 0.000 0.000
RQ_IOCTL: 0 0 0 0 0.000 0.000
RQ_FLUSH: 0 0 0 0 0.000 0.000
RQ_FSYNC: 0 0 0 0 0.000 0.000
RQ_FASYNC: 0 0 0 0 0.000 0.000
RQ_LOCK: 0 0 0 0 0.000 0.000
RQ_LINK: 0 0 0 0 0.000 0.000
RQ_SYMLINK: 0 0 0 0 0.000 0.000
RQ_MKDIR: 0 0 0 0 0.000 0.000
RQ_RMDIR: 0 0 0 0 0.000 0.000
RQ_MKNOD: 0 0 0 0 0.000 0.000
RQ_RENAME: 0 0 0 0 0.000 0.000
RQ_READLINK: 2 0 0 0 0.000 0.000
RQ_TRUNCATE: 0 0 0 0 0.000 0.000
RQ_SETATTR: 0 0 0 0 0.000 0.000
RQ_GETATTR: 72 0 0 0 0.000 0.000
RQ_PARALLEL_READ: 0 0 0 0 0.000 0.000
RQ_PARALLEL_WRITE: 0 0 0 0 0.000 0.000
RQ_STATFS: 21 0 0 0 0.000 0.000
RQ_READPAGE_ASYNC: 0 0 0 0 0.000 0.000
RQ_READPAGE_DATA: 0 0 0 0 0.000 0.000
RQ_GETEOI: 0 0 0 0 0.000 0.000
RQ_SETXATTR: 0 0 0 0 0.000 0.000
RQ_GETXATTR: 0 0 0 0 0.000 0.000
RQ_LISTXATTR: 0 0 0 0 0.000 0.000
RQ_REMOVEXATTR: 0 0 0 0 0.000 0.000
RQ_VERIFYFS: 0 0 0 0 0.000 0.000
RQ_RO_CACHE_DISABLE: 0 0 0 0 0.000 0.000
RQ_PERMISSION: 4 0 0 0 0.000 0.000
RQ_SYNC_UPDATE: 0 0 0 0 0.000 0.000
RQ_READPAGES_RQ: 29360 0 0 0 0.000 0.000
RQ_READPAGES_RP: 0 0 0 0 0.000 0.000
RQ_WRITEPAGES_RQ: 0 0 0 0 0.000 0.000
RQ_WRITEPAGES_RP: 0 0 0 0 0.000 0.000
llseek: 0 0 0.000 0.000
read: 0 0 0.000 0.000
aio_read: 951107 8 0.000 15.004
write: 0 0 0.000 0.000
aio_write: 0 0 0.000 0.000
readdir: 113550 0 0.000 0.796
unlocked_ioctl: 0 2 0.000 0.000
mmap: 0 0 0.000 0.000
open: 59362 0 0.000 0.012
flush: 228300 0 0.000 0.004
release: 59360 0 0.000 0.004
fsync: 0 0 0.000 0.000
fasync: 0 0 0.000 0.000
lock: 0 0 0.000 0.000
flock: 0 0 0.000 0.000
writepage: 0 0 0.000 0.000
writepages: 0 0 0.000 0.000
readpage: 160 0 0.000 0.004
readpages: 26627 0 0.000 0.744
write_begin: 0 0 0.000 0.000
write_end: 0 0 0.000 0.000
direct_io: 0 0 0.000 0.000
statfs: 21 0 0.000 0.004
put_super: 0 0 0.000 0.000
write_super: 0 0 0.000 0.000
evict_inode: 3 0 0.000 0.000
show_options: 81525 0 0.000 0.004
d_create: 0 0 0.000 0.000
d_lookup: 974281 0 0.000 0.016
d_link: 0 0 0.000 0.000
d_unlink: 0 0 0.000 0.000
d_symlink: 0 0 0.000 0.000
d_mkdir: 0 0 0.000 0.000
d_rmdir: 0 0 0.000 0.000
d_mknod: 0 0 0.000 0.000
d_rename: 0 0 0.000 0.000
d_truncate: 0 0 0.000 0.000
d_permission: 1149779 1090389 0.004 0.004
d_setattr: 0 0 0.000 0.000
d_getattr: 172814 0 0.004 0.004
d_setxattr: 0 0 0.000 0.000
d_getxattr: 0 0 0.000 0.000
d_listxattr: 0 0 0.000 0.000
d_removexattr: 0 0 0.000 0.000
f_create: 0 0 0.000 0.000
f_link: 0 0 0.000 0.000
f_unlink: 0 0 0.000 0.000
f_symlink: 0 0 0.000 0.000
f_mkdir: 0 0 0.000 0.000
f_rmdir: 0 0 0.000 0.000
f_mknod: 0 0 0.000 0.000
f_rename: 0 0 0.000 0.000
f_truncate: 0 0 0.000 0.000
f_permission: 2 0 0.000 0.000
f_setattr: 0 0 0.000 0.000
f_getattr: 858514 0 0.000 0.004
f_setxattr: 0 0 0.000 0.000
f_getxattr: 0 0 0.000 0.000
f_listxattr: 0 0 0.000 0.000
f_removexattr: 0 0 0.000 0.000
l_readlink: 0 0 0.000 0.000
l_follow_link: 0 0 0.000 0.000
l_put_link: 0 0 0.000 0.000
l_setattr: 0 0 0.000 0.000
l_getattr: 59088 0 0.000 0.004
d_revalidate: 113449 0 0.000 0.000
read_min_max: 0 0
write_min_max: 0 0
IPC requests: 0 0
IPC async requests: 0 0
IPC replies: 0 0
Open files: 2
Inodes created: 968132
Inodes removed: 30
"""),
            Src("/proc/fs/dvs/mounts/1/stats",
"""\
RQ_LOOKUP: 974286 0 0 0 0.000 0.000
RQ_OPEN: 59362 0 0 0 0.000 0.000
RQ_CLOSE: 59360 0 0 0 0.000 0.000
RQ_READDIR: 113550 0 0 0 0.000 0.000
RQ_CREATE: 0 0 0 0 0.000 0.000
RQ_UNLINK: 0 0 0 0 0.000 0.000
RQ_IOCTL: 0 0 0 0 0.000 0.000
RQ_FLUSH: 0 0 0 0 0.000 0.000
RQ_FSYNC: 0 0 0 0 0.000 0.000
RQ_FASYNC: 0 0 0 0 0.000 0.000
RQ_LOCK: 0 0 0 0 0.000 0.000
RQ_LINK: 0 0 0 0 0.000 0.000
RQ_SYMLINK: 0 0 0 0 0.000 0.000
RQ_MKDIR: 0 0 0 0 0.000 0.000
RQ_RMDIR: 0 0 0 0 0.000 0.000
RQ_MKNOD: 0 0 0 0 0.000 0.000
RQ_RENAME: 0 0 0 0 0.000 0.000
RQ_READLINK: 2 0 0 0 0.000 0.000
RQ_TRUNCATE: 0 0 0 0 0.000 0.000
RQ_SETATTR: 0 0 0 0 0.000 0.000
RQ_GETATTR: 72 0 0 0 0.000 0.000
RQ_PARALLEL_READ: 0 0 0 0 0.000 0.000
RQ_PARALLEL_WRITE: 0 0 0 0 0.000 0.000
RQ_STATFS: 21 0 0 0 0.000 0.000
RQ_READPAGE_ASYNC: 0 0 0 0 0.000 0.000
RQ_READPAGE_DATA: 0 0 0 0 0.000 0.000
RQ_GETEOI: 0 0 0 0 0.000 0.000
RQ_SETXATTR: 0 0 0 0 0.000 0.000
RQ_GETXATTR: 0 0 0 0 0.000 0.000
RQ_LISTXATTR: 0 0 0 0 0.000 0.000
RQ_REMOVEXATTR: 0 0 0 0 0.000 0.000
RQ_VERIFYFS: 0 0 0 0 0.000 0.000
RQ_RO_CACHE_DISABLE: 0 0 0 0 0.000 0.000
RQ_PERMISSION: 4 0 0 0 0.000 0.000
RQ_SYNC_UPDATE: 0 0 0 0 0.000 0.000
RQ_READPAGES_RQ: 29360 0 0 0 0.000 0.000
RQ_READPAGES_RP: 0 0 0 0 0.000 0.000
RQ_WRITEPAGES_RQ: 0 0 0 0 0.000 0.000
RQ_WRITEPAGES_RP: 0 0 0 0 0.000 0.000
llseek: 0 0 0.000 0.000
read: 0 0 0.000 0.000
aio_read: 951107 8 0.000 15.004
write: 0 0 0.000 0.000
aio_write: 0 0 0.000 0.000
readdir: 113550 0 0.000 0.796
unlocked_ioctl: 0 2 0.000 0.000
mmap: 0 0 0.000 0.000
open: 59362 0 0.000 0.012
flush: 228300 0 0.000 0.004
release: 59360 0 0.000 0.004
fsync: 0 0 0.000 0.000
fasync: 0 0 0.000 0.000
lock: 0 0 0.000 0.000
flock: 0 0 0.000 0.000
writepage: 0 0 0.000 0.000
writepages: 0 0 0.000 0.000
readpage: 160 0 0.000 0.004
readpages: 26627 0 0.000 0.744
write_begin: 0 0 0.000 0.000
write_end: 0 0 0.000 0.000
direct_io: 0 0 0.000 0.000
statfs: 21 0 0.000 0.004
put_super: 0 0 0.000 0.000
write_super: 0 0 0.000 0.000
evict_inode: 3 0 0.000 0.000
show_options: 81525 0 0.000 0.004
d_create: 0 0 0.000 0.000
d_lookup: 974281 0 0.000 0.016
d_link: 0 0 0.000 0.000
d_unlink: 0 0 0.000 0.000
d_symlink: 0 0 0.000 0.000
d_mkdir: 0 0 0.000 0.000
d_rmdir: 0 0 0.000 0.000
d_mknod: 0 0 0.000 0.000
d_rename: 0 0 0.000 0.000
d_truncate: 0 0 0.000 0.000
d_permission: 1149779 1090389 0.004 0.004
d_setattr: 0 0 0.000 0.000
d_getattr: 172814 0 0.004 0.004
d_setxattr: 0 0 0.000 0.000
d_getxattr: 0 0 0.000 0.000
d_listxattr: 0 0 0.000 0.000
d_removexattr: 0 0 0.000 0.000
f_create: 0 0 0.000 0.000
f_link: 0 0 0.000 0.000
f_unlink: 0 0 0.000 0.000
f_symlink: 0 0 0.000 0.000
f_mkdir: 0 0 0.000 0.000
f_rmdir: 0 0 0.000 0.000
f_mknod: 0 0 0.000 0.000
f_rename: 0 0 0.000 0.000
f_truncate: 0 0 0.000 0.000
f_permission: 2 0 0.000 0.000
f_setattr: 0 0 0.000 0.000
f_getattr: 858514 0 0.000 0.004
f_setxattr: 0 0 0.000 0.000
f_getxattr: 0 0 0.000 0.000
f_listxattr: 0 0 0.000 0.000
f_removexattr: 0 0 0.000 0.000
l_readlink: 0 0 0.000 0.000
l_follow_link: 0 0 0.000 0.000
l_put_link: 0 0 0.000 0.000
l_setattr: 0 0 0.000 0.000
l_getattr: 59088 0 0.000 0.004
d_revalidate: 113449 0 0.000 0.000
read_min_max: 0 0
write_min_max: 0 0
IPC requests: 0 0
IPC async requests: 0 0
IPC replies: 0 0
Open files: 2
Inodes created: 968132
Inodes removed: 40
"""),
        ],
        [
            LDMSData({
                "instance_name": LDMSD_PREFIX + "/var/opt/cray/imps",
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "mountpt": "/var/opt/cray/imps",
                    "RQ_LOOKUP_clnt_cnt": 974286,
                    "RQ_LOOKUP_clnt_err": 0,
                    "RQ_LOOKUP_srvr_cnt": 0,
                    "RQ_LOOKUP_srvr_err": 0,
                    "RQ_LOOKUP_last_dur": 0.000,
                    "RQ_LOOKUP_max_dur": 0.000,
                    "RQ_OPEN_clnt_cnt": 59362,
                    "RQ_OPEN_clnt_err": 0,
                    "RQ_OPEN_srvr_cnt": 0,
                    "RQ_OPEN_srvr_err": 0,
                    "RQ_OPEN_last_dur": 0.000,
                    "RQ_OPEN_max_dur": 0.000,
                    "RQ_CLOSE_clnt_cnt": 59360,
                    "RQ_CLOSE_clnt_err": 0,
                    "RQ_CLOSE_srvr_cnt": 0,
                    "RQ_CLOSE_srvr_err": 0,
                    "RQ_CLOSE_last_dur": 0.000,
                    "RQ_CLOSE_max_dur": 0.000,
                    "RQ_READDIR_clnt_cnt": 113550,
                    "RQ_READDIR_clnt_err": 0,
                    "RQ_READDIR_srvr_cnt": 0,
                    "RQ_READDIR_srvr_err": 0,
                    "RQ_READDIR_last_dur": 0.000,
                    "RQ_READDIR_max_dur": 0.000,
                    "RQ_CREATE_clnt_cnt": 0,
                    "RQ_CREATE_clnt_err": 0,
                    "RQ_CREATE_srvr_cnt": 0,
                    "RQ_CREATE_srvr_err": 0,
                    "RQ_CREATE_last_dur": 0.000,
                    "RQ_CREATE_max_dur": 0.000,
                    "RQ_UNLINK_clnt_cnt": 0,
                    "RQ_UNLINK_clnt_err": 0,
                    "RQ_UNLINK_srvr_cnt": 0,
                    "RQ_UNLINK_srvr_err": 0,
                    "RQ_UNLINK_last_dur": 0.000,
                    "RQ_UNLINK_max_dur": 0.000,
                    "RQ_IOCTL_clnt_cnt": 0,
                    "RQ_IOCTL_clnt_err": 0,
                    "RQ_IOCTL_srvr_cnt": 0,
                    "RQ_IOCTL_srvr_err": 0,
                    "RQ_IOCTL_last_dur": 0.000,
                    "RQ_IOCTL_max_dur": 0.000,
                    "RQ_FLUSH_clnt_cnt": 0,
                    "RQ_FLUSH_clnt_err": 0,
                    "RQ_FLUSH_srvr_cnt": 0,
                    "RQ_FLUSH_srvr_err": 0,
                    "RQ_FLUSH_last_dur": 0.000,
                    "RQ_FLUSH_max_dur": 0.000,
                    "RQ_FSYNC_clnt_cnt": 0,
                    "RQ_FSYNC_clnt_err": 0,
                    "RQ_FSYNC_srvr_cnt": 0,
                    "RQ_FSYNC_srvr_err": 0,
                    "RQ_FSYNC_last_dur": 0.000,
                    "RQ_FSYNC_max_dur": 0.000,
                    "RQ_FASYNC_clnt_cnt": 0,
                    "RQ_FASYNC_clnt_err": 0,
                    "RQ_FASYNC_srvr_cnt": 0,
                    "RQ_FASYNC_srvr_err": 0,
                    "RQ_FASYNC_last_dur": 0.000,
                    "RQ_FASYNC_max_dur": 0.000,
                    "RQ_LOCK_clnt_cnt": 0,
                    "RQ_LOCK_clnt_err": 0,
                    "RQ_LOCK_srvr_cnt": 0,
                    "RQ_LOCK_srvr_err": 0,
                    "RQ_LOCK_last_dur": 0.000,
                    "RQ_LOCK_max_dur": 0.000,
                    "RQ_LINK_clnt_cnt": 0,
                    "RQ_LINK_clnt_err": 0,
                    "RQ_LINK_srvr_cnt": 0,
                    "RQ_LINK_srvr_err": 0,
                    "RQ_LINK_last_dur": 0.000,
                    "RQ_LINK_max_dur": 0.000,
                    "RQ_SYMLINK_clnt_cnt": 0,
                    "RQ_SYMLINK_clnt_err": 0,
                    "RQ_SYMLINK_srvr_cnt": 0,
                    "RQ_SYMLINK_srvr_err": 0,
                    "RQ_SYMLINK_last_dur": 0.000,
                    "RQ_SYMLINK_max_dur": 0.000,
                    "RQ_MKDIR_clnt_cnt": 0,
                    "RQ_MKDIR_clnt_err": 0,
                    "RQ_MKDIR_srvr_cnt": 0,
                    "RQ_MKDIR_srvr_err": 0,
                    "RQ_MKDIR_last_dur": 0.000,
                    "RQ_MKDIR_max_dur": 0.000,
                    "RQ_RMDIR_clnt_cnt": 0,
                    "RQ_RMDIR_clnt_err": 0,
                    "RQ_RMDIR_srvr_cnt": 0,
                    "RQ_RMDIR_srvr_err": 0,
                    "RQ_RMDIR_last_dur": 0.000,
                    "RQ_RMDIR_max_dur": 0.000,
                    "RQ_MKNOD_clnt_cnt": 0,
                    "RQ_MKNOD_clnt_err": 0,
                    "RQ_MKNOD_srvr_cnt": 0,
                    "RQ_MKNOD_srvr_err": 0,
                    "RQ_MKNOD_last_dur": 0.000,
                    "RQ_MKNOD_max_dur": 0.000,
                    "RQ_RENAME_clnt_cnt": 0,
                    "RQ_RENAME_clnt_err": 0,
                    "RQ_RENAME_srvr_cnt": 0,
                    "RQ_RENAME_srvr_err": 0,
                    "RQ_RENAME_last_dur": 0.000,
                    "RQ_RENAME_max_dur": 0.000,
                    "RQ_READLINK_clnt_cnt": 2,
                    "RQ_READLINK_clnt_err": 0,
                    "RQ_READLINK_srvr_cnt": 0,
                    "RQ_READLINK_srvr_err": 0,
                    "RQ_READLINK_last_dur": 0.000,
                    "RQ_READLINK_max_dur": 0.000,
                    "RQ_TRUNCATE_clnt_cnt": 0,
                    "RQ_TRUNCATE_clnt_err": 0,
                    "RQ_TRUNCATE_srvr_cnt": 0,
                    "RQ_TRUNCATE_srvr_err": 0,
                    "RQ_TRUNCATE_last_dur": 0.000,
                    "RQ_TRUNCATE_max_dur": 0.000,
                    "RQ_SETATTR_clnt_cnt": 0,
                    "RQ_SETATTR_clnt_err": 0,
                    "RQ_SETATTR_srvr_cnt": 0,
                    "RQ_SETATTR_srvr_err": 0,
                    "RQ_SETATTR_last_dur": 0.000,
                    "RQ_SETATTR_max_dur": 0.000,
                    "RQ_GETATTR_clnt_cnt": 72,
                    "RQ_GETATTR_clnt_err": 0,
                    "RQ_GETATTR_srvr_cnt": 0,
                    "RQ_GETATTR_srvr_err": 0,
                    "RQ_GETATTR_last_dur": 0.000,
                    "RQ_GETATTR_max_dur": 0.000,
                    "RQ_PARALLEL_READ_clnt_cnt": 0,
                    "RQ_PARALLEL_READ_clnt_err": 0,
                    "RQ_PARALLEL_READ_srvr_cnt": 0,
                    "RQ_PARALLEL_READ_srvr_err": 0,
                    "RQ_PARALLEL_READ_last_dur": 0.000,
                    "RQ_PARALLEL_READ_max_dur": 0.000,
                    "RQ_PARALLEL_WRITE_clnt_cnt": 0,
                    "RQ_PARALLEL_WRITE_clnt_err": 0,
                    "RQ_PARALLEL_WRITE_srvr_cnt": 0,
                    "RQ_PARALLEL_WRITE_srvr_err": 0,
                    "RQ_PARALLEL_WRITE_last_dur": 0.000,
                    "RQ_PARALLEL_WRITE_max_dur": 0.000,
                    "RQ_STATFS_clnt_cnt": 21,
                    "RQ_STATFS_clnt_err": 0,
                    "RQ_STATFS_srvr_cnt": 0,
                    "RQ_STATFS_srvr_err": 0,
                    "RQ_STATFS_last_dur": 0.000,
                    "RQ_STATFS_max_dur": 0.000,
                    "RQ_READPAGE_ASYNC_clnt_cnt": 0,
                    "RQ_READPAGE_ASYNC_clnt_err": 0,
                    "RQ_READPAGE_ASYNC_srvr_cnt": 0,
                    "RQ_READPAGE_ASYNC_srvr_err": 0,
                    "RQ_READPAGE_ASYNC_last_dur": 0.000,
                    "RQ_READPAGE_ASYNC_max_dur": 0.000,
                    "RQ_READPAGE_DATA_clnt_cnt": 0,
                    "RQ_READPAGE_DATA_clnt_err": 0,
                    "RQ_READPAGE_DATA_srvr_cnt": 0,
                    "RQ_READPAGE_DATA_srvr_err": 0,
                    "RQ_READPAGE_DATA_last_dur": 0.000,
                    "RQ_READPAGE_DATA_max_dur": 0.000,
                    "RQ_GETEOI_clnt_cnt": 0,
                    "RQ_GETEOI_clnt_err": 0,
                    "RQ_GETEOI_srvr_cnt": 0,
                    "RQ_GETEOI_srvr_err": 0,
                    "RQ_GETEOI_last_dur": 0.000,
                    "RQ_GETEOI_max_dur": 0.000,
                    "RQ_SETXATTR_clnt_cnt": 0,
                    "RQ_SETXATTR_clnt_err": 0,
                    "RQ_SETXATTR_srvr_cnt": 0,
                    "RQ_SETXATTR_srvr_err": 0,
                    "RQ_SETXATTR_last_dur": 0.000,
                    "RQ_SETXATTR_max_dur": 0.000,
                    "RQ_GETXATTR_clnt_cnt": 0,
                    "RQ_GETXATTR_clnt_err": 0,
                    "RQ_GETXATTR_srvr_cnt": 0,
                    "RQ_GETXATTR_srvr_err": 0,
                    "RQ_GETXATTR_last_dur": 0.000,
                    "RQ_GETXATTR_max_dur": 0.000,
                    "RQ_LISTXATTR_clnt_cnt": 0,
                    "RQ_LISTXATTR_clnt_err": 0,
                    "RQ_LISTXATTR_srvr_cnt": 0,
                    "RQ_LISTXATTR_srvr_err": 0,
                    "RQ_LISTXATTR_last_dur": 0.000,
                    "RQ_LISTXATTR_max_dur": 0.000,
                    "RQ_REMOVEXATTR_clnt_cnt": 0,
                    "RQ_REMOVEXATTR_clnt_err": 0,
                    "RQ_REMOVEXATTR_srvr_cnt": 0,
                    "RQ_REMOVEXATTR_srvr_err": 0,
                    "RQ_REMOVEXATTR_last_dur": 0.000,
                    "RQ_REMOVEXATTR_max_dur": 0.000,
                    "RQ_VERIFYFS_clnt_cnt": 0,
                    "RQ_VERIFYFS_clnt_err": 0,
                    "RQ_VERIFYFS_srvr_cnt": 0,
                    "RQ_VERIFYFS_srvr_err": 0,
                    "RQ_VERIFYFS_last_dur": 0.000,
                    "RQ_VERIFYFS_max_dur": 0.000,
                    "RQ_RO_CACHE_DISABLE_clnt_cnt": 0,
                    "RQ_RO_CACHE_DISABLE_clnt_err": 0,
                    "RQ_RO_CACHE_DISABLE_srvr_cnt": 0,
                    "RQ_RO_CACHE_DISABLE_srvr_err": 0,
                    "RQ_RO_CACHE_DISABLE_last_dur": 0.000,
                    "RQ_RO_CACHE_DISABLE_max_dur": 0.000,
                    "RQ_PERMISSION_clnt_cnt": 4,
                    "RQ_PERMISSION_clnt_err": 0,
                    "RQ_PERMISSION_srvr_cnt": 0,
                    "RQ_PERMISSION_srvr_err": 0,
                    "RQ_PERMISSION_last_dur": 0.000,
                    "RQ_PERMISSION_max_dur": 0.000,
                    "RQ_SYNC_UPDATE_clnt_cnt": 0,
                    "RQ_SYNC_UPDATE_clnt_err": 0,
                    "RQ_SYNC_UPDATE_srvr_cnt": 0,
                    "RQ_SYNC_UPDATE_srvr_err": 0,
                    "RQ_SYNC_UPDATE_last_dur": 0.000,
                    "RQ_SYNC_UPDATE_max_dur": 0.000,
                    "RQ_READPAGES_RQ_clnt_cnt": 29360,
                    "RQ_READPAGES_RQ_clnt_err": 0,
                    "RQ_READPAGES_RQ_srvr_cnt": 0,
                    "RQ_READPAGES_RQ_srvr_err": 0,
                    "RQ_READPAGES_RQ_last_dur": 0.000,
                    "RQ_READPAGES_RQ_max_dur": 0.000,
                    "RQ_READPAGES_RP_clnt_cnt": 0,
                    "RQ_READPAGES_RP_clnt_err": 0,
                    "RQ_READPAGES_RP_srvr_cnt": 0,
                    "RQ_READPAGES_RP_srvr_err": 0,
                    "RQ_READPAGES_RP_last_dur": 0.000,
                    "RQ_READPAGES_RP_max_dur": 0.000,
                    "RQ_WRITEPAGES_RQ_clnt_cnt": 0,
                    "RQ_WRITEPAGES_RQ_clnt_err": 0,
                    "RQ_WRITEPAGES_RQ_srvr_cnt": 0,
                    "RQ_WRITEPAGES_RQ_srvr_err": 0,
                    "RQ_WRITEPAGES_RQ_last_dur": 0.000,
                    "RQ_WRITEPAGES_RQ_max_dur": 0.000,
                    "RQ_WRITEPAGES_RP_clnt_cnt": 0,
                    "RQ_WRITEPAGES_RP_clnt_err": 0,
                    "RQ_WRITEPAGES_RP_srvr_cnt": 0,
                    "RQ_WRITEPAGES_RP_srvr_err": 0,
                    "RQ_WRITEPAGES_RP_last_dur": 0.000,
                    "RQ_WRITEPAGES_RP_max_dur": 0.000,
                    "llseek_cnt": 0,
                    "llseek_err": 0,
                    "llseek_last_dur": 0.000,
                    "llseek_max_dur": 0.000,
                    "read_cnt": 0,
                    "read_err": 0,
                    "read_last_dur": 0.000,
                    "read_max_dur": 0.000,
                    "aio_read_cnt": 951107,
                    "aio_read_err": 8,
                    "aio_read_last_dur": 0.000,
                    "aio_read_max_dur": 15.004,
                    "write_cnt": 0,
                    "write_err": 0,
                    "write_last_dur": 0.000,
                    "write_max_dur": 0.000,
                    "aio_write_cnt": 0,
                    "aio_write_err": 0,
                    "aio_write_last_dur": 0.000,
                    "aio_write_max_dur": 0.000,
                    "readdir_cnt": 113550,
                    "readdir_err": 0,
                    "readdir_last_dur": 0.000,
                    "readdir_max_dur": 0.796,
                    "unlocked_ioctl_cnt": 0,
                    "unlocked_ioctl_err": 2,
                    "unlocked_ioctl_last_dur": 0.000,
                    "unlocked_ioctl_max_dur": 0.000,
                    "mmap_cnt": 0,
                    "mmap_err": 0,
                    "mmap_last_dur": 0.000,
                    "mmap_max_dur": 0.000,
                    "open_cnt": 59362,
                    "open_err": 0,
                    "open_last_dur": 0.000,
                    "open_max_dur": 0.012,
                    "flush_cnt": 228300,
                    "flush_err": 0,
                    "flush_last_dur": 0.000,
                    "flush_max_dur": 0.004,
                    "release_cnt": 59360,
                    "release_err": 0,
                    "release_last_dur": 0.000,
                    "release_max_dur": 0.004,
                    "fsync_cnt": 0,
                    "fsync_err": 0,
                    "fsync_last_dur": 0.000,
                    "fsync_max_dur": 0.000,
                    "fasync_cnt": 0,
                    "fasync_err": 0,
                    "fasync_last_dur": 0.000,
                    "fasync_max_dur": 0.000,
                    "lock_cnt": 0,
                    "lock_err": 0,
                    "lock_last_dur": 0.000,
                    "lock_max_dur": 0.000,
                    "flock_cnt": 0,
                    "flock_err": 0,
                    "flock_last_dur": 0.000,
                    "flock_max_dur": 0.000,
                    "writepage_cnt": 0,
                    "writepage_err": 0,
                    "writepage_last_dur": 0.000,
                    "writepage_max_dur": 0.000,
                    "writepages_cnt": 0,
                    "writepages_err": 0,
                    "writepages_last_dur": 0.000,
                    "writepages_max_dur": 0.000,
                    "readpage_cnt": 160,
                    "readpage_err": 0,
                    "readpage_last_dur": 0.000,
                    "readpage_max_dur": 0.004,
                    "readpages_cnt": 26627,
                    "readpages_err": 0,
                    "readpages_last_dur": 0.000,
                    "readpages_max_dur": 0.744,
                    "write_begin_cnt": 0,
                    "write_begin_err": 0,
                    "write_begin_last_dur": 0.000,
                    "write_begin_max_dur": 0.000,
                    "write_end_cnt": 0,
                    "write_end_err": 0,
                    "write_end_last_dur": 0.000,
                    "write_end_max_dur": 0.000,
                    "direct_io_cnt": 0,
                    "direct_io_err": 0,
                    "direct_io_last_dur": 0.000,
                    "direct_io_max_dur": 0.000,
                    "statfs_cnt": 21,
                    "statfs_err": 0,
                    "statfs_last_dur": 0.000,
                    "statfs_max_dur": 0.004,
                    "put_super_cnt": 0,
                    "put_super_err": 0,
                    "put_super_last_dur": 0.000,
                    "put_super_max_dur": 0.000,
                    "write_super_cnt": 0,
                    "write_super_err": 0,
                    "write_super_last_dur": 0.000,
                    "write_super_max_dur": 0.000,
                    "evict_inode_cnt": 3,
                    "evict_inode_err": 0,
                    "evict_inode_last_dur": 0.000,
                    "evict_inode_max_dur": 0.000,
                    "show_options_cnt": 81525,
                    "show_options_err": 0,
                    "show_options_last_dur": 0.000,
                    "show_options_max_dur": 0.004,
                    "d_create_cnt": 0,
                    "d_create_err": 0,
                    "d_create_last_dur": 0.000,
                    "d_create_max_dur": 0.000,
                    "d_lookup_cnt": 974281,
                    "d_lookup_err": 0,
                    "d_lookup_last_dur": 0.000,
                    "d_lookup_max_dur": 0.016,
                    "d_link_cnt": 0,
                    "d_link_err": 0,
                    "d_link_last_dur": 0.000,
                    "d_link_max_dur": 0.000,
                    "d_unlink_cnt": 0,
                    "d_unlink_err": 0,
                    "d_unlink_last_dur": 0.000,
                    "d_unlink_max_dur": 0.000,
                    "d_symlink_cnt": 0,
                    "d_symlink_err": 0,
                    "d_symlink_last_dur": 0.000,
                    "d_symlink_max_dur": 0.000,
                    "d_mkdir_cnt": 0,
                    "d_mkdir_err": 0,
                    "d_mkdir_last_dur": 0.000,
                    "d_mkdir_max_dur": 0.000,
                    "d_rmdir_cnt": 0,
                    "d_rmdir_err": 0,
                    "d_rmdir_last_dur": 0.000,
                    "d_rmdir_max_dur": 0.000,
                    "d_mknod_cnt": 0,
                    "d_mknod_err": 0,
                    "d_mknod_last_dur": 0.000,
                    "d_mknod_max_dur": 0.000,
                    "d_rename_cnt": 0,
                    "d_rename_err": 0,
                    "d_rename_last_dur": 0.000,
                    "d_rename_max_dur": 0.000,
                    "d_truncate_cnt": 0,
                    "d_truncate_err": 0,
                    "d_truncate_last_dur": 0.000,
                    "d_truncate_max_dur": 0.000,
                    "d_permission_cnt": 1149779,
                    "d_permission_err": 1090389,
                    "d_permission_last_dur": 0.004,
                    "d_permission_max_dur": 0.004,
                    "d_setattr_cnt": 0,
                    "d_setattr_err": 0,
                    "d_setattr_last_dur": 0.000,
                    "d_setattr_max_dur": 0.000,
                    "d_getattr_cnt": 172814,
                    "d_getattr_err": 0,
                    "d_getattr_last_dur": 0.004,
                    "d_getattr_max_dur": 0.004,
                    "d_setxattr_cnt": 0,
                    "d_setxattr_err": 0,
                    "d_setxattr_last_dur": 0.000,
                    "d_setxattr_max_dur": 0.000,
                    "d_getxattr_cnt": 0,
                    "d_getxattr_err": 0,
                    "d_getxattr_last_dur": 0.000,
                    "d_getxattr_max_dur": 0.000,
                    "d_listxattr_cnt": 0,
                    "d_listxattr_err": 0,
                    "d_listxattr_last_dur": 0.000,
                    "d_listxattr_max_dur": 0.000,
                    "d_removexattr_cnt": 0,
                    "d_removexattr_err": 0,
                    "d_removexattr_last_dur": 0.000,
                    "d_removexattr_max_dur": 0.000,
                    "f_create_cnt": 0,
                    "f_create_err": 0,
                    "f_create_last_dur": 0.000,
                    "f_create_max_dur": 0.000,
                    "f_link_cnt": 0,
                    "f_link_err": 0,
                    "f_link_last_dur": 0.000,
                    "f_link_max_dur": 0.000,
                    "f_unlink_cnt": 0,
                    "f_unlink_err": 0,
                    "f_unlink_last_dur": 0.000,
                    "f_unlink_max_dur": 0.000,
                    "f_symlink_cnt": 0,
                    "f_symlink_err": 0,
                    "f_symlink_last_dur": 0.000,
                    "f_symlink_max_dur": 0.000,
                    "f_mkdir_cnt": 0,
                    "f_mkdir_err": 0,
                    "f_mkdir_last_dur": 0.000,
                    "f_mkdir_max_dur": 0.000,
                    "f_rmdir_cnt": 0,
                    "f_rmdir_err": 0,
                    "f_rmdir_last_dur": 0.000,
                    "f_rmdir_max_dur": 0.000,
                    "f_mknod_cnt": 0,
                    "f_mknod_err": 0,
                    "f_mknod_last_dur": 0.000,
                    "f_mknod_max_dur": 0.000,
                    "f_rename_cnt": 0,
                    "f_rename_err": 0,
                    "f_rename_last_dur": 0.000,
                    "f_rename_max_dur": 0.000,
                    "f_truncate_cnt": 0,
                    "f_truncate_err": 0,
                    "f_truncate_last_dur": 0.000,
                    "f_truncate_max_dur": 0.000,
                    "f_permission_cnt": 2,
                    "f_permission_err": 0,
                    "f_permission_last_dur": 0.000,
                    "f_permission_max_dur": 0.000,
                    "f_setattr_cnt": 0,
                    "f_setattr_err": 0,
                    "f_setattr_last_dur": 0.000,
                    "f_setattr_max_dur": 0.000,
                    "f_getattr_cnt": 858514,
                    "f_getattr_err": 0,
                    "f_getattr_last_dur": 0.000,
                    "f_getattr_max_dur": 0.004,
                    "f_setxattr_cnt": 0,
                    "f_setxattr_err": 0,
                    "f_setxattr_last_dur": 0.000,
                    "f_setxattr_max_dur": 0.000,
                    "f_getxattr_cnt": 0,
                    "f_getxattr_err": 0,
                    "f_getxattr_last_dur": 0.000,
                    "f_getxattr_max_dur": 0.000,
                    "f_listxattr_cnt": 0,
                    "f_listxattr_err": 0,
                    "f_listxattr_last_dur": 0.000,
                    "f_listxattr_max_dur": 0.000,
                    "f_removexattr_cnt": 0,
                    "f_removexattr_err": 0,
                    "f_removexattr_last_dur": 0.000,
                    "f_removexattr_max_dur": 0.000,
                    "l_readlink_cnt": 0,
                    "l_readlink_err": 0,
                    "l_readlink_last_dur": 0.000,
                    "l_readlink_max_dur": 0.000,
                    "l_follow_link_cnt": 0,
                    "l_follow_link_err": 0,
                    "l_follow_link_last_dur": 0.000,
                    "l_follow_link_max_dur": 0.000,
                    "l_put_link_cnt": 0,
                    "l_put_link_err": 0,
                    "l_put_link_last_dur": 0.000,
                    "l_put_link_max_dur": 0.000,
                    "l_setattr_cnt": 0,
                    "l_setattr_err": 0,
                    "l_setattr_last_dur": 0.000,
                    "l_setattr_max_dur": 0.000,
                    "l_getattr_cnt": 59088,
                    "l_getattr_err": 0,
                    "l_getattr_last_dur": 0.000,
                    "l_getattr_max_dur": 0.004,
                    "d_revalidate_cnt": 113449,
                    "d_revalidate_err": 0,
                    "d_revalidate_last_dur": 0.000,
                    "d_revalidate_max_dur": 0.000,
                    "read_min_max_min": 0,
                    "read_min_max_max": 0,
                    "write_min_max_min": 0,
                    "write_min_max_max": 0,
                    "IPC_requests_cnt": 0,
                    "IPC_requests_err": 0,
                    "IPC_async_requests_cnt": 0,
                    "IPC_async_requests_err": 0,
                    "IPC_replies_cnt": 0,
                    "IPC_replies_err": 0,
                    "Open_files": 2,
                    "Inodes_created": 968132,
                    "Inodes_removed": 30,
                }
            }),
            LDMSData({
                "instance_name": LDMSD_PREFIX + "/test/path",
                "schema_name": SCHEMA_NAME,
                "metrics": {
                    "mountpt": "/test/path",
                    "RQ_LOOKUP_clnt_cnt": 974286,
                    "RQ_LOOKUP_clnt_err": 0,
                    "RQ_LOOKUP_srvr_cnt": 0,
                    "RQ_LOOKUP_srvr_err": 0,
                    "RQ_LOOKUP_last_dur": 0.000,
                    "RQ_LOOKUP_max_dur": 0.000,
                    "RQ_OPEN_clnt_cnt": 59362,
                    "RQ_OPEN_clnt_err": 0,
                    "RQ_OPEN_srvr_cnt": 0,
                    "RQ_OPEN_srvr_err": 0,
                    "RQ_OPEN_last_dur": 0.000,
                    "RQ_OPEN_max_dur": 0.000,
                    "RQ_CLOSE_clnt_cnt": 59360,
                    "RQ_CLOSE_clnt_err": 0,
                    "RQ_CLOSE_srvr_cnt": 0,
                    "RQ_CLOSE_srvr_err": 0,
                    "RQ_CLOSE_last_dur": 0.000,
                    "RQ_CLOSE_max_dur": 0.000,
                    "RQ_READDIR_clnt_cnt": 113550,
                    "RQ_READDIR_clnt_err": 0,
                    "RQ_READDIR_srvr_cnt": 0,
                    "RQ_READDIR_srvr_err": 0,
                    "RQ_READDIR_last_dur": 0.000,
                    "RQ_READDIR_max_dur": 0.000,
                    "RQ_CREATE_clnt_cnt": 0,
                    "RQ_CREATE_clnt_err": 0,
                    "RQ_CREATE_srvr_cnt": 0,
                    "RQ_CREATE_srvr_err": 0,
                    "RQ_CREATE_last_dur": 0.000,
                    "RQ_CREATE_max_dur": 0.000,
                    "RQ_UNLINK_clnt_cnt": 0,
                    "RQ_UNLINK_clnt_err": 0,
                    "RQ_UNLINK_srvr_cnt": 0,
                    "RQ_UNLINK_srvr_err": 0,
                    "RQ_UNLINK_last_dur": 0.000,
                    "RQ_UNLINK_max_dur": 0.000,
                    "RQ_IOCTL_clnt_cnt": 0,
                    "RQ_IOCTL_clnt_err": 0,
                    "RQ_IOCTL_srvr_cnt": 0,
                    "RQ_IOCTL_srvr_err": 0,
                    "RQ_IOCTL_last_dur": 0.000,
                    "RQ_IOCTL_max_dur": 0.000,
                    "RQ_FLUSH_clnt_cnt": 0,
                    "RQ_FLUSH_clnt_err": 0,
                    "RQ_FLUSH_srvr_cnt": 0,
                    "RQ_FLUSH_srvr_err": 0,
                    "RQ_FLUSH_last_dur": 0.000,
                    "RQ_FLUSH_max_dur": 0.000,
                    "RQ_FSYNC_clnt_cnt": 0,
                    "RQ_FSYNC_clnt_err": 0,
                    "RQ_FSYNC_srvr_cnt": 0,
                    "RQ_FSYNC_srvr_err": 0,
                    "RQ_FSYNC_last_dur": 0.000,
                    "RQ_FSYNC_max_dur": 0.000,
                    "RQ_FASYNC_clnt_cnt": 0,
                    "RQ_FASYNC_clnt_err": 0,
                    "RQ_FASYNC_srvr_cnt": 0,
                    "RQ_FASYNC_srvr_err": 0,
                    "RQ_FASYNC_last_dur": 0.000,
                    "RQ_FASYNC_max_dur": 0.000,
                    "RQ_LOCK_clnt_cnt": 0,
                    "RQ_LOCK_clnt_err": 0,
                    "RQ_LOCK_srvr_cnt": 0,
                    "RQ_LOCK_srvr_err": 0,
                    "RQ_LOCK_last_dur": 0.000,
                    "RQ_LOCK_max_dur": 0.000,
                    "RQ_LINK_clnt_cnt": 0,
                    "RQ_LINK_clnt_err": 0,
                    "RQ_LINK_srvr_cnt": 0,
                    "RQ_LINK_srvr_err": 0,
                    "RQ_LINK_last_dur": 0.000,
                    "RQ_LINK_max_dur": 0.000,
                    "RQ_SYMLINK_clnt_cnt": 0,
                    "RQ_SYMLINK_clnt_err": 0,
                    "RQ_SYMLINK_srvr_cnt": 0,
                    "RQ_SYMLINK_srvr_err": 0,
                    "RQ_SYMLINK_last_dur": 0.000,
                    "RQ_SYMLINK_max_dur": 0.000,
                    "RQ_MKDIR_clnt_cnt": 0,
                    "RQ_MKDIR_clnt_err": 0,
                    "RQ_MKDIR_srvr_cnt": 0,
                    "RQ_MKDIR_srvr_err": 0,
                    "RQ_MKDIR_last_dur": 0.000,
                    "RQ_MKDIR_max_dur": 0.000,
                    "RQ_RMDIR_clnt_cnt": 0,
                    "RQ_RMDIR_clnt_err": 0,
                    "RQ_RMDIR_srvr_cnt": 0,
                    "RQ_RMDIR_srvr_err": 0,
                    "RQ_RMDIR_last_dur": 0.000,
                    "RQ_RMDIR_max_dur": 0.000,
                    "RQ_MKNOD_clnt_cnt": 0,
                    "RQ_MKNOD_clnt_err": 0,
                    "RQ_MKNOD_srvr_cnt": 0,
                    "RQ_MKNOD_srvr_err": 0,
                    "RQ_MKNOD_last_dur": 0.000,
                    "RQ_MKNOD_max_dur": 0.000,
                    "RQ_RENAME_clnt_cnt": 0,
                    "RQ_RENAME_clnt_err": 0,
                    "RQ_RENAME_srvr_cnt": 0,
                    "RQ_RENAME_srvr_err": 0,
                    "RQ_RENAME_last_dur": 0.000,
                    "RQ_RENAME_max_dur": 0.000,
                    "RQ_READLINK_clnt_cnt": 2,
                    "RQ_READLINK_clnt_err": 0,
                    "RQ_READLINK_srvr_cnt": 0,
                    "RQ_READLINK_srvr_err": 0,
                    "RQ_READLINK_last_dur": 0.000,
                    "RQ_READLINK_max_dur": 0.000,
                    "RQ_TRUNCATE_clnt_cnt": 0,
                    "RQ_TRUNCATE_clnt_err": 0,
                    "RQ_TRUNCATE_srvr_cnt": 0,
                    "RQ_TRUNCATE_srvr_err": 0,
                    "RQ_TRUNCATE_last_dur": 0.000,
                    "RQ_TRUNCATE_max_dur": 0.000,
                    "RQ_SETATTR_clnt_cnt": 0,
                    "RQ_SETATTR_clnt_err": 0,
                    "RQ_SETATTR_srvr_cnt": 0,
                    "RQ_SETATTR_srvr_err": 0,
                    "RQ_SETATTR_last_dur": 0.000,
                    "RQ_SETATTR_max_dur": 0.000,
                    "RQ_GETATTR_clnt_cnt": 72,
                    "RQ_GETATTR_clnt_err": 0,
                    "RQ_GETATTR_srvr_cnt": 0,
                    "RQ_GETATTR_srvr_err": 0,
                    "RQ_GETATTR_last_dur": 0.000,
                    "RQ_GETATTR_max_dur": 0.000,
                    "RQ_PARALLEL_READ_clnt_cnt": 0,
                    "RQ_PARALLEL_READ_clnt_err": 0,
                    "RQ_PARALLEL_READ_srvr_cnt": 0,
                    "RQ_PARALLEL_READ_srvr_err": 0,
                    "RQ_PARALLEL_READ_last_dur": 0.000,
                    "RQ_PARALLEL_READ_max_dur": 0.000,
                    "RQ_PARALLEL_WRITE_clnt_cnt": 0,
                    "RQ_PARALLEL_WRITE_clnt_err": 0,
                    "RQ_PARALLEL_WRITE_srvr_cnt": 0,
                    "RQ_PARALLEL_WRITE_srvr_err": 0,
                    "RQ_PARALLEL_WRITE_last_dur": 0.000,
                    "RQ_PARALLEL_WRITE_max_dur": 0.000,
                    "RQ_STATFS_clnt_cnt": 21,
                    "RQ_STATFS_clnt_err": 0,
                    "RQ_STATFS_srvr_cnt": 0,
                    "RQ_STATFS_srvr_err": 0,
                    "RQ_STATFS_last_dur": 0.000,
                    "RQ_STATFS_max_dur": 0.000,
                    "RQ_READPAGE_ASYNC_clnt_cnt": 0,
                    "RQ_READPAGE_ASYNC_clnt_err": 0,
                    "RQ_READPAGE_ASYNC_srvr_cnt": 0,
                    "RQ_READPAGE_ASYNC_srvr_err": 0,
                    "RQ_READPAGE_ASYNC_last_dur": 0.000,
                    "RQ_READPAGE_ASYNC_max_dur": 0.000,
                    "RQ_READPAGE_DATA_clnt_cnt": 0,
                    "RQ_READPAGE_DATA_clnt_err": 0,
                    "RQ_READPAGE_DATA_srvr_cnt": 0,
                    "RQ_READPAGE_DATA_srvr_err": 0,
                    "RQ_READPAGE_DATA_last_dur": 0.000,
                    "RQ_READPAGE_DATA_max_dur": 0.000,
                    "RQ_GETEOI_clnt_cnt": 0,
                    "RQ_GETEOI_clnt_err": 0,
                    "RQ_GETEOI_srvr_cnt": 0,
                    "RQ_GETEOI_srvr_err": 0,
                    "RQ_GETEOI_last_dur": 0.000,
                    "RQ_GETEOI_max_dur": 0.000,
                    "RQ_SETXATTR_clnt_cnt": 0,
                    "RQ_SETXATTR_clnt_err": 0,
                    "RQ_SETXATTR_srvr_cnt": 0,
                    "RQ_SETXATTR_srvr_err": 0,
                    "RQ_SETXATTR_last_dur": 0.000,
                    "RQ_SETXATTR_max_dur": 0.000,
                    "RQ_GETXATTR_clnt_cnt": 0,
                    "RQ_GETXATTR_clnt_err": 0,
                    "RQ_GETXATTR_srvr_cnt": 0,
                    "RQ_GETXATTR_srvr_err": 0,
                    "RQ_GETXATTR_last_dur": 0.000,
                    "RQ_GETXATTR_max_dur": 0.000,
                    "RQ_LISTXATTR_clnt_cnt": 0,
                    "RQ_LISTXATTR_clnt_err": 0,
                    "RQ_LISTXATTR_srvr_cnt": 0,
                    "RQ_LISTXATTR_srvr_err": 0,
                    "RQ_LISTXATTR_last_dur": 0.000,
                    "RQ_LISTXATTR_max_dur": 0.000,
                    "RQ_REMOVEXATTR_clnt_cnt": 0,
                    "RQ_REMOVEXATTR_clnt_err": 0,
                    "RQ_REMOVEXATTR_srvr_cnt": 0,
                    "RQ_REMOVEXATTR_srvr_err": 0,
                    "RQ_REMOVEXATTR_last_dur": 0.000,
                    "RQ_REMOVEXATTR_max_dur": 0.000,
                    "RQ_VERIFYFS_clnt_cnt": 0,
                    "RQ_VERIFYFS_clnt_err": 0,
                    "RQ_VERIFYFS_srvr_cnt": 0,
                    "RQ_VERIFYFS_srvr_err": 0,
                    "RQ_VERIFYFS_last_dur": 0.000,
                    "RQ_VERIFYFS_max_dur": 0.000,
                    "RQ_RO_CACHE_DISABLE_clnt_cnt": 0,
                    "RQ_RO_CACHE_DISABLE_clnt_err": 0,
                    "RQ_RO_CACHE_DISABLE_srvr_cnt": 0,
                    "RQ_RO_CACHE_DISABLE_srvr_err": 0,
                    "RQ_RO_CACHE_DISABLE_last_dur": 0.000,
                    "RQ_RO_CACHE_DISABLE_max_dur": 0.000,
                    "RQ_PERMISSION_clnt_cnt": 4,
                    "RQ_PERMISSION_clnt_err": 0,
                    "RQ_PERMISSION_srvr_cnt": 0,
                    "RQ_PERMISSION_srvr_err": 0,
                    "RQ_PERMISSION_last_dur": 0.000,
                    "RQ_PERMISSION_max_dur": 0.000,
                    "RQ_SYNC_UPDATE_clnt_cnt": 0,
                    "RQ_SYNC_UPDATE_clnt_err": 0,
                    "RQ_SYNC_UPDATE_srvr_cnt": 0,
                    "RQ_SYNC_UPDATE_srvr_err": 0,
                    "RQ_SYNC_UPDATE_last_dur": 0.000,
                    "RQ_SYNC_UPDATE_max_dur": 0.000,
                    "RQ_READPAGES_RQ_clnt_cnt": 29360,
                    "RQ_READPAGES_RQ_clnt_err": 0,
                    "RQ_READPAGES_RQ_srvr_cnt": 0,
                    "RQ_READPAGES_RQ_srvr_err": 0,
                    "RQ_READPAGES_RQ_last_dur": 0.000,
                    "RQ_READPAGES_RQ_max_dur": 0.000,
                    "RQ_READPAGES_RP_clnt_cnt": 0,
                    "RQ_READPAGES_RP_clnt_err": 0,
                    "RQ_READPAGES_RP_srvr_cnt": 0,
                    "RQ_READPAGES_RP_srvr_err": 0,
                    "RQ_READPAGES_RP_last_dur": 0.000,
                    "RQ_READPAGES_RP_max_dur": 0.000,
                    "RQ_WRITEPAGES_RQ_clnt_cnt": 0,
                    "RQ_WRITEPAGES_RQ_clnt_err": 0,
                    "RQ_WRITEPAGES_RQ_srvr_cnt": 0,
                    "RQ_WRITEPAGES_RQ_srvr_err": 0,
                    "RQ_WRITEPAGES_RQ_last_dur": 0.000,
                    "RQ_WRITEPAGES_RQ_max_dur": 0.000,
                    "RQ_WRITEPAGES_RP_clnt_cnt": 0,
                    "RQ_WRITEPAGES_RP_clnt_err": 0,
                    "RQ_WRITEPAGES_RP_srvr_cnt": 0,
                    "RQ_WRITEPAGES_RP_srvr_err": 0,
                    "RQ_WRITEPAGES_RP_last_dur": 0.000,
                    "RQ_WRITEPAGES_RP_max_dur": 0.000,
                    "llseek_cnt": 0,
                    "llseek_err": 0,
                    "llseek_last_dur": 0.000,
                    "llseek_max_dur": 0.000,
                    "read_cnt": 0,
                    "read_err": 0,
                    "read_last_dur": 0.000,
                    "read_max_dur": 0.000,
                    "aio_read_cnt": 951107,
                    "aio_read_err": 8,
                    "aio_read_last_dur": 0.000,
                    "aio_read_max_dur": 15.004,
                    "write_cnt": 0,
                    "write_err": 0,
                    "write_last_dur": 0.000,
                    "write_max_dur": 0.000,
                    "aio_write_cnt": 0,
                    "aio_write_err": 0,
                    "aio_write_last_dur": 0.000,
                    "aio_write_max_dur": 0.000,
                    "readdir_cnt": 113550,
                    "readdir_err": 0,
                    "readdir_last_dur": 0.000,
                    "readdir_max_dur": 0.796,
                    "unlocked_ioctl_cnt": 0,
                    "unlocked_ioctl_err": 2,
                    "unlocked_ioctl_last_dur": 0.000,
                    "unlocked_ioctl_max_dur": 0.000,
                    "mmap_cnt": 0,
                    "mmap_err": 0,
                    "mmap_last_dur": 0.000,
                    "mmap_max_dur": 0.000,
                    "open_cnt": 59362,
                    "open_err": 0,
                    "open_last_dur": 0.000,
                    "open_max_dur": 0.012,
                    "flush_cnt": 228300,
                    "flush_err": 0,
                    "flush_last_dur": 0.000,
                    "flush_max_dur": 0.004,
                    "release_cnt": 59360,
                    "release_err": 0,
                    "release_last_dur": 0.000,
                    "release_max_dur": 0.004,
                    "fsync_cnt": 0,
                    "fsync_err": 0,
                    "fsync_last_dur": 0.000,
                    "fsync_max_dur": 0.000,
                    "fasync_cnt": 0,
                    "fasync_err": 0,
                    "fasync_last_dur": 0.000,
                    "fasync_max_dur": 0.000,
                    "lock_cnt": 0,
                    "lock_err": 0,
                    "lock_last_dur": 0.000,
                    "lock_max_dur": 0.000,
                    "flock_cnt": 0,
                    "flock_err": 0,
                    "flock_last_dur": 0.000,
                    "flock_max_dur": 0.000,
                    "writepage_cnt": 0,
                    "writepage_err": 0,
                    "writepage_last_dur": 0.000,
                    "writepage_max_dur": 0.000,
                    "writepages_cnt": 0,
                    "writepages_err": 0,
                    "writepages_last_dur": 0.000,
                    "writepages_max_dur": 0.000,
                    "readpage_cnt": 160,
                    "readpage_err": 0,
                    "readpage_last_dur": 0.000,
                    "readpage_max_dur": 0.004,
                    "readpages_cnt": 26627,
                    "readpages_err": 0,
                    "readpages_last_dur": 0.000,
                    "readpages_max_dur": 0.744,
                    "write_begin_cnt": 0,
                    "write_begin_err": 0,
                    "write_begin_last_dur": 0.000,
                    "write_begin_max_dur": 0.000,
                    "write_end_cnt": 0,
                    "write_end_err": 0,
                    "write_end_last_dur": 0.000,
                    "write_end_max_dur": 0.000,
                    "direct_io_cnt": 0,
                    "direct_io_err": 0,
                    "direct_io_last_dur": 0.000,
                    "direct_io_max_dur": 0.000,
                    "statfs_cnt": 21,
                    "statfs_err": 0,
                    "statfs_last_dur": 0.000,
                    "statfs_max_dur": 0.004,
                    "put_super_cnt": 0,
                    "put_super_err": 0,
                    "put_super_last_dur": 0.000,
                    "put_super_max_dur": 0.000,
                    "write_super_cnt": 0,
                    "write_super_err": 0,
                    "write_super_last_dur": 0.000,
                    "write_super_max_dur": 0.000,
                    "evict_inode_cnt": 3,
                    "evict_inode_err": 0,
                    "evict_inode_last_dur": 0.000,
                    "evict_inode_max_dur": 0.000,
                    "show_options_cnt": 81525,
                    "show_options_err": 0,
                    "show_options_last_dur": 0.000,
                    "show_options_max_dur": 0.004,
                    "d_create_cnt": 0,
                    "d_create_err": 0,
                    "d_create_last_dur": 0.000,
                    "d_create_max_dur": 0.000,
                    "d_lookup_cnt": 974281,
                    "d_lookup_err": 0,
                    "d_lookup_last_dur": 0.000,
                    "d_lookup_max_dur": 0.016,
                    "d_link_cnt": 0,
                    "d_link_err": 0,
                    "d_link_last_dur": 0.000,
                    "d_link_max_dur": 0.000,
                    "d_unlink_cnt": 0,
                    "d_unlink_err": 0,
                    "d_unlink_last_dur": 0.000,
                    "d_unlink_max_dur": 0.000,
                    "d_symlink_cnt": 0,
                    "d_symlink_err": 0,
                    "d_symlink_last_dur": 0.000,
                    "d_symlink_max_dur": 0.000,
                    "d_mkdir_cnt": 0,
                    "d_mkdir_err": 0,
                    "d_mkdir_last_dur": 0.000,
                    "d_mkdir_max_dur": 0.000,
                    "d_rmdir_cnt": 0,
                    "d_rmdir_err": 0,
                    "d_rmdir_last_dur": 0.000,
                    "d_rmdir_max_dur": 0.000,
                    "d_mknod_cnt": 0,
                    "d_mknod_err": 0,
                    "d_mknod_last_dur": 0.000,
                    "d_mknod_max_dur": 0.000,
                    "d_rename_cnt": 0,
                    "d_rename_err": 0,
                    "d_rename_last_dur": 0.000,
                    "d_rename_max_dur": 0.000,
                    "d_truncate_cnt": 0,
                    "d_truncate_err": 0,
                    "d_truncate_last_dur": 0.000,
                    "d_truncate_max_dur": 0.000,
                    "d_permission_cnt": 1149779,
                    "d_permission_err": 1090389,
                    "d_permission_last_dur": 0.004,
                    "d_permission_max_dur": 0.004,
                    "d_setattr_cnt": 0,
                    "d_setattr_err": 0,
                    "d_setattr_last_dur": 0.000,
                    "d_setattr_max_dur": 0.000,
                    "d_getattr_cnt": 172814,
                    "d_getattr_err": 0,
                    "d_getattr_last_dur": 0.004,
                    "d_getattr_max_dur": 0.004,
                    "d_setxattr_cnt": 0,
                    "d_setxattr_err": 0,
                    "d_setxattr_last_dur": 0.000,
                    "d_setxattr_max_dur": 0.000,
                    "d_getxattr_cnt": 0,
                    "d_getxattr_err": 0,
                    "d_getxattr_last_dur": 0.000,
                    "d_getxattr_max_dur": 0.000,
                    "d_listxattr_cnt": 0,
                    "d_listxattr_err": 0,
                    "d_listxattr_last_dur": 0.000,
                    "d_listxattr_max_dur": 0.000,
                    "d_removexattr_cnt": 0,
                    "d_removexattr_err": 0,
                    "d_removexattr_last_dur": 0.000,
                    "d_removexattr_max_dur": 0.000,
                    "f_create_cnt": 0,
                    "f_create_err": 0,
                    "f_create_last_dur": 0.000,
                    "f_create_max_dur": 0.000,
                    "f_link_cnt": 0,
                    "f_link_err": 0,
                    "f_link_last_dur": 0.000,
                    "f_link_max_dur": 0.000,
                    "f_unlink_cnt": 0,
                    "f_unlink_err": 0,
                    "f_unlink_last_dur": 0.000,
                    "f_unlink_max_dur": 0.000,
                    "f_symlink_cnt": 0,
                    "f_symlink_err": 0,
                    "f_symlink_last_dur": 0.000,
                    "f_symlink_max_dur": 0.000,
                    "f_mkdir_cnt": 0,
                    "f_mkdir_err": 0,
                    "f_mkdir_last_dur": 0.000,
                    "f_mkdir_max_dur": 0.000,
                    "f_rmdir_cnt": 0,
                    "f_rmdir_err": 0,
                    "f_rmdir_last_dur": 0.000,
                    "f_rmdir_max_dur": 0.000,
                    "f_mknod_cnt": 0,
                    "f_mknod_err": 0,
                    "f_mknod_last_dur": 0.000,
                    "f_mknod_max_dur": 0.000,
                    "f_rename_cnt": 0,
                    "f_rename_err": 0,
                    "f_rename_last_dur": 0.000,
                    "f_rename_max_dur": 0.000,
                    "f_truncate_cnt": 0,
                    "f_truncate_err": 0,
                    "f_truncate_last_dur": 0.000,
                    "f_truncate_max_dur": 0.000,
                    "f_permission_cnt": 2,
                    "f_permission_err": 0,
                    "f_permission_last_dur": 0.000,
                    "f_permission_max_dur": 0.000,
                    "f_setattr_cnt": 0,
                    "f_setattr_err": 0,
                    "f_setattr_last_dur": 0.000,
                    "f_setattr_max_dur": 0.000,
                    "f_getattr_cnt": 858514,
                    "f_getattr_err": 0,
                    "f_getattr_last_dur": 0.000,
                    "f_getattr_max_dur": 0.004,
                    "f_setxattr_cnt": 0,
                    "f_setxattr_err": 0,
                    "f_setxattr_last_dur": 0.000,
                    "f_setxattr_max_dur": 0.000,
                    "f_getxattr_cnt": 0,
                    "f_getxattr_err": 0,
                    "f_getxattr_last_dur": 0.000,
                    "f_getxattr_max_dur": 0.000,
                    "f_listxattr_cnt": 0,
                    "f_listxattr_err": 0,
                    "f_listxattr_last_dur": 0.000,
                    "f_listxattr_max_dur": 0.000,
                    "f_removexattr_cnt": 0,
                    "f_removexattr_err": 0,
                    "f_removexattr_last_dur": 0.000,
                    "f_removexattr_max_dur": 0.000,
                    "l_readlink_cnt": 0,
                    "l_readlink_err": 0,
                    "l_readlink_last_dur": 0.000,
                    "l_readlink_max_dur": 0.000,
                    "l_follow_link_cnt": 0,
                    "l_follow_link_err": 0,
                    "l_follow_link_last_dur": 0.000,
                    "l_follow_link_max_dur": 0.000,
                    "l_put_link_cnt": 0,
                    "l_put_link_err": 0,
                    "l_put_link_last_dur": 0.000,
                    "l_put_link_max_dur": 0.000,
                    "l_setattr_cnt": 0,
                    "l_setattr_err": 0,
                    "l_setattr_last_dur": 0.000,
                    "l_setattr_max_dur": 0.000,
                    "l_getattr_cnt": 59088,
                    "l_getattr_err": 0,
                    "l_getattr_last_dur": 0.000,
                    "l_getattr_max_dur": 0.004,
                    "d_revalidate_cnt": 113449,
                    "d_revalidate_err": 0,
                    "d_revalidate_last_dur": 0.000,
                    "d_revalidate_max_dur": 0.000,
                    "read_min_max_min": 0,
                    "read_min_max_max": 0,
                    "write_min_max_min": 0,
                    "write_min_max_max": 0,
                    "IPC_requests_cnt": 0,
                    "IPC_requests_err": 0,
                    "IPC_async_requests_cnt": 0,
                    "IPC_async_requests_err": 0,
                    "IPC_replies_cnt": 0,
                    "IPC_replies_err": 0,
                    "Open_files": 2,
                    "Inodes_created": 968132,
                    "Inodes_removed": 40,
                }
            }),
        ]
    ),
]

class DVSTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"

    @classmethod
    def getPluginName(cls):
        return "dvs_sampler"

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

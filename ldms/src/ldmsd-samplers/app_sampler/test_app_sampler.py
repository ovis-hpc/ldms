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

# This script tests for data correctness of app_sampler plugin (all metrics,
# multiple sets).

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

HOSTNAME = socket.gethostname()
LDMSD_PREFIX = HOSTNAME + ":" + str(LDMSChrootTest.PORT)
DIR = "test_app_sampler" # a work directory for this test, so that everything is in
                     # one place
JOB_ID = 5

if not os.path.exists(DIR):
    os.mkdir(DIR)

def array_sum(*args):
    return reduce(lambda a,b: a + b, args)

class PIDSrcData(object):
    """Generate Src and LDMSData for PID"""
    def __init__(self, pid, job_id, task_rank, prdcr = None, root = "/"):
        self.pid = pid
        self.job_id = job_id
        self.task_rank = task_rank
        self.prdcr = prdcr if prdcr else ("{}:{}".format(HOSTNAME, LDMSChrootTest.PORT))
        self.root = root

    def getSrc(self, gen):
        """Returns [ Src() ] data for PID"""
        return array_sum(
                self.cmdline_src(gen),
                self.n_open_files_src(gen),
                self.io_src(gen),
                self.oom_score_src(gen),
                self.oom_score_adj_src(gen),
                self.stat_src(gen),
                self.status_src(gen),
                self.syscall_src(gen),
                self.timerslack_ns_src(gen),
                self.wchan_src(gen),
            )

    def getLDMSData(self, gen):
        return LDMSData({
            "instance_name": "{}/{}/{}".format(self.prdcr, self.job_id, self.pid),
            "schema_name": "app_sampler",
            "metrics": dict(
                self.common_metrics(gen).items() +
                self.cmdline_metrics(gen).items() +
                self.n_open_files_metrics(gen).items() +
                self.io_metrics(gen).items() +
                self.oom_score_metrics(gen).items() +
                self.oom_score_adj_metrics(gen).items() +
                self.stat_metrics(gen).items() +
                self.status_metrics(gen).items() +
                self.syscall_metrics(gen).items() +
                self.timerslack_ns_metrics(gen).items() +
                self.wchan_metrics(gen).items() +
                self.root_metrics(gen).items()
            )
        })

    def common_metrics(self, gen):
        return {
            "component_id": LDMSChrootTest.COMPONENT_ID,
            "app_id": 0,
            "job_id": self.job_id,
            "task_rank": self.task_rank,
        }

    def cmdline_src(self, gen):
        path = "/proc/{}/cmdline".format(self.pid)
        content = "/bin/prog{}\0arg1\0arg2\0".format(self.pid)
        return [ Src(path, content) ]

    def cmdline_metrics(self, gen):
        s = self.cmdline_src(gen)
        _s = s[0].content
        return { "cmdline": _s, "cmdline_len": len(_s) }

    def n_open_files_src(self, gen):
        path = "/proc/{}/fd/".format(self.pid)
        N = self.pid + gen
        ret = []
        for i in range(0, N):
            _p = path + "/" + str(i)
            _c = str(i)
            ret.append( Src(_p, _c) )
        return ret

    def n_open_files_metrics(self, gen):
        return { "n_open_files": self.pid + gen }

    def io_src(self, gen):
        path = "/proc/{}/io".format(self.pid)
        data = [
            ("rchar", self.pid + gen + 7),
            ("wchar", self.pid + gen + 6),
            ("syscr", self.pid + gen + 5),
            ("syscw", self.pid + gen + 4),
            ("read_bytes", self.pid + gen + 3),
            ("write_bytes", self.pid + gen + 2),
            ("cancelled_write_bytes", self.pid + gen + 1),
        ]
        content = "".join( "{}: {}\n".format(k, v) for k, v in data )
        return [ Src(path, content) ]

    def io_metrics(self, gen):
        return dict([
            ("io_read_b", self.pid + gen + 7),
            ("io_write_b", self.pid + gen + 6),
            ("io_n_read", self.pid + gen + 5),
            ("io_n_write", self.pid + gen + 4),
            ("io_read_dev_b", self.pid + gen + 3),
            ("io_write_dev_b", self.pid + gen + 2),
            ("io_write_cancelled_b", self.pid + gen + 1),
        ])

    def oom_score_src(self, gen):
        return [ Src("/proc/{}/oom_score".format(self.pid),
                     str(self.pid + gen + 8) ) ]

    def oom_score_metrics(self, gen):
        return { "oom_score": self.pid + gen + 8 }

    def oom_score_adj_src(self, gen):
        return [ Src("/proc/{}/oom_score_adj".format(self.pid),
                     str(self.pid + gen + 9) ) ]

    def oom_score_adj_metrics(self, gen):
        return { "oom_score_adj": self.pid + gen + 9 }

    def stat_src(self, gen):
        path = "/proc/{}/stat".format(self.pid)
        state = 'S' if gen == 0 else 'R'
        _lead = "{0} (/bin/prog{0}) {1} ".format(self.pid, state)
        _nums = " ".join( str(10 + self.pid + gen + x) for x in range(0, 49) )
        return [ Src(path, _lead + _nums) ]

    def stat_metrics(self, gen):
        state = 'S' if gen == 0 else 'R'
        _lead = [ self.pid, "/bin/prog{0}".format(self.pid), state ]
        _nums = [ (10 + self.pid + gen + x) for x in range(0, 49) ]
        _names = [
            "stat_pid", "stat_comm", "stat_state", "stat_ppid", "stat_pgrp",
            "stat_session", "stat_tty_nr", "stat_tpgid", "stat_flags",
            "stat_minflt", "stat_cminflt", "stat_majflt", "stat_cmajflt",
            "stat_utime", "stat_stime", "stat_cutime", "stat_cstime",
            "stat_priority", "stat_nice", "stat_num_threads",
            "stat_itrealvalue", "stat_starttime", "stat_vsize", "stat_rss",
            "stat_rsslim", "stat_startcode", "stat_endcode", "stat_startstack",
            "stat_kstkesp", "stat_kstkeip", "stat_signal", "stat_blocked",
            "stat_sigignore", "stat_sigcatch", "stat_wchan", "stat_nswap",
            "stat_cnswap", "stat_exit_signal", "stat_processor",
            "stat_rt_priority", "stat_policy", "stat_delayacct_blkio_ticks",
            "stat_guest_time", "stat_cguest_time", "stat_start_data",
            "stat_end_data", "stat_start_brk", "stat_arg_start", "stat_arg_end",
            "stat_env_start", "stat_env_end", "stat_exit_code",
        ]
        return dict(zip(_names, _lead + _nums))

    def status_src(self, gen):
        _state = "R (running)" if gen else "S (sleeping)"
        _data = [
            "Name:\tprog{}\n".format(self.pid),
            "Umask:\t0022\n",
            "State:\t{}\n".format(_state),
            "Tgid:\t{}\n".format(self.pid + 70),
            "Ngid:\t{}\n".format(self.pid + 71),
            "Pid:\t{}\n".format(self.pid),
            "PPid:\t{}\n".format(self.pid + 100),
            "TracerPid:\t0\n",
            "Uid:\t1000\t1000\t1000\t1000\n",
            "Gid:\t2000\t2000\t2000\t2000\n",
            "FDSize:\t{}\n".format(self.pid + 105),
            "Groups:\t4 24 27 30 46 115 127 1000\n",
            "NStgid:\t{}\n".format(self.pid + 106),
            "NSpid:\t{}\n".format(self.pid + 107),
            "NSpgid:\t{}\n".format(self.pid + 108),
            "NSsid:\t{}\n".format(self.pid + 109),
            "VmPeak:\t{:8} kB\n".format(self.pid + (gen + 1)*4096),
            "VmSize:\t{:8} kB\n".format(self.pid + (gen + 2)*4096),
            "VmLck:\t{:8} kB\n".format(self.pid + 112 + gen),
            "VmPin:\t{:8} kB\n".format(self.pid + 113 + gen),
            "VmHWM:\t{:8} kB\n".format(self.pid + 114 + gen),
            "VmRSS:\t{:8} kB\n".format(self.pid + 115 + gen),
            "RssAnon:\t{:8} kB\n".format(self.pid + 116 + gen),
            "RssFile:\t{:8} kB\n".format(self.pid + 117 + gen),
            "RssShmem:\t{:8} kB\n".format(self.pid + 118 + gen),
            "VmData:\t{:8} kB\n".format(self.pid + 119 + gen),
            "VmStk:\t{:8} kB\n".format(self.pid + 120 + gen),
            "VmExe:\t{:8} kB\n".format(self.pid + 121 + gen),
            "VmLib:\t{:8} kB\n".format(self.pid + 122 + gen),
            "VmPTE:\t{:8} kB\n".format(self.pid + 123 + gen),
            "VmPMD:\t{:8} kB\n".format(self.pid + 223 + gen),
            "VmSwap:\t{:8} kB\n".format(self.pid + 124 + gen),
            "HugetlbPages:\t{:8} kB\n".format(self.pid + 125 + gen),
            "CoreDumping:\t{:d}\n".format(bool(gen)),
            "Threads:\t{}\n".format(self.pid + gen),
            "SigQ:\t{}/126175\n".format(self.pid + gen),
            "SigPnd:\t{:16x}\n".format(self.pid + 128 + gen),
            "ShdPnd:\t{:16x}\n".format(self.pid + 129 + gen),
            "SigBlk:\t{:16x}\n".format(self.pid + 130 + gen),
            "SigIgn:\t{:16x}\n".format(self.pid + 131 + gen),
            "SigCgt:\t{:16x}\n".format(self.pid + 132 + gen),
            "CapInh:\t{:16x}\n".format(self.pid + 133 + gen),
            "CapPrm:\t{:16x}\n".format(self.pid + 134 + gen),
            "CapEff:\t{:16x}\n".format(self.pid + 135 + gen),
            "CapBnd:\t{:16x}\n".format(self.pid + 136 + gen),
            "CapAmb:\t{:16x}\n".format(self.pid + 137 + gen),
            "NoNewPrivs:\t{}\n".format(self.pid + 138 + gen),
            "Seccomp:\t{}\n".format(self.pid + 139 + gen),
            "Speculation_Store_Bypass:\t{}\n".format("vulnerable" if gen else "not vulnerable"),
            "Cpus_allowed:\tff\n",
            "Cpus_allowed_list:\t0-7\n",
            "Mems_allowed:\tff000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000001\n",
            "Mems_allowed_list:\t0,1015-1023\n",
            "voluntary_ctxt_switches:\t{}\n".format(self.pid + 150 + gen),
            "nonvoluntary_ctxt_switches:\t{}\n".format(self.pid + 151 + gen),
        ]
        content = "".join(_data)
        return [ Src( "/proc/{}/status".format(self.pid), content) ]

    def status_metrics(self, gen):
        _state = "R" if gen else "S"
        _metrics = [
            ("status_name", "prog{}".format(self.pid)),
            ("status_umask", int(0022)),
            ("status_state", "{}".format(_state)),
            ("status_tgid", self.pid + 70),
            ("status_ngid", self.pid + 71),
            ("status_pid", self.pid),
            ("status_ppid", self.pid + 100),
            ("status_tracerpid", 0),
            ("status_uid", (1000, 1000, 1000, 1000)),
            ("status_gid", (2000, 2000, 2000, 2000)),
            ("status_fdsize", self.pid + 105),
            ("status_groups", (4, 24, 27, 30, 46, 115, 127, 1000)),
            ("status_nstgid", (self.pid + 106,) + (0,)*15),
            ("status_nspid" , (self.pid + 107,) + (0,)*15),
            ("status_nspgid", (self.pid + 108,) + (0,)*15),
            ("status_nssid" , (self.pid + 109,) + (0,)*15),
            ("status_vmpeak", self.pid + (gen + 1)*4096 ),
            ("status_vmsize", self.pid + (gen + 2)*4096 ),
            ("status_vmlck", self.pid + 112 + gen),
            ("status_vmpin", self.pid + 113 + gen),
            ("status_vmhwm", self.pid + 114 + gen),
            ("status_vmrss", self.pid + 115 + gen),
            ("status_rssanon", self.pid + 116 + gen),
            ("status_rssfile", self.pid + 117 + gen),
            ("status_rssshmem", self.pid + 118 + gen),
            ("status_vmdata", self.pid + 119 + gen),
            ("status_vmstk", self.pid + 120 + gen),
            ("status_vmexe", self.pid + 121 + gen),
            ("status_vmlib", self.pid + 122 + gen),
            ("status_vmpte", self.pid + 123 + gen),
            ("status_vmpmd", self.pid + 223 + gen),
            ("status_vmswap", self.pid + 124 + gen),
            ("status_hugetlbpages", self.pid + 125 + gen),
            ("status_coredumping", bool(gen)),
            ("status_threads", self.pid + gen),
            ("status_sig_queued", self.pid + gen),
            ("status_sig_limit", 126175),
            ("status_sigpnd", self.pid + 128 + gen),
            ("status_shdpnd", self.pid + 129 + gen),
            ("status_sigblk", self.pid + 130 + gen),
            ("status_sigign", self.pid + 131 + gen),
            ("status_sigcgt", self.pid + 132 + gen),
            ("status_capinh", self.pid + 133 + gen),
            ("status_capprm", self.pid + 134 + gen),
            ("status_capeff", self.pid + 135 + gen),
            ("status_capbnd", self.pid + 136 + gen),
            ("status_capamb", self.pid + 137 + gen),
            ("status_nonewprivs", self.pid + 138 + gen),
            ("status_seccomp", self.pid + 139 + gen),
            ("status_speculation_store_bypass", "vulnerable" if gen else "not vulnerable"),
            ("status_cpus_allowed", (0xff,)),
            ("status_cpus_allowed_list", "0-7"),
            ("status_mems_allowed", (0x1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xff000000)),
            ("status_mems_allowed_list", "0,1015-1023"),
            ("status_voluntary_ctxt_switches", self.pid + 150 + gen),
            ("status_nonvoluntary_ctxt_switches", self.pid + 151 + gen),
        ]
        return dict(_metrics)

    def syscall_src(self, gen):
        path = "/proc/{}/syscall".format(self.pid)
        if gen == 0:
            content = "running"
        else:
            content = "{:d} {:x} {:x} {:x} {:x} {:x} {:x} {:x} {:x}".format(
                        self.pid + gen + 10,
                        self.pid + gen + 11,
                        self.pid + gen + 12,
                        self.pid + gen + 13,
                        self.pid + gen + 14,
                        self.pid + gen + 15,
                        self.pid + gen + 16,
                        self.pid + gen + 17,
                        self.pid + gen + 18,
                    )
        return [ Src(path, content) ]

    def syscall_metrics(self, gen):
        if gen == 0:
            return { "syscall": (0,) * 9 }
        else:
            return { "syscall": (
                        self.pid + gen + 10,
                        self.pid + gen + 11,
                        self.pid + gen + 12,
                        self.pid + gen + 13,
                        self.pid + gen + 14,
                        self.pid + gen + 15,
                        self.pid + gen + 16,
                        self.pid + gen + 17,
                        self.pid + gen + 18,
                    ) }

    def timerslack_ns_src(self, gen):
        return [ Src("/proc/{}/timerslack_ns".format(self.pid),
                     str(self.pid + gen + 500)) ]

    def timerslack_ns_metrics(self, gen):
        return { "timerslack_ns": self.pid + gen + 500 }

    def wchan_src(self, gen):
        return [ Src("/proc/{}/wchan".format(self.pid),
                     "symbol{}.{}".format(self.pid, gen)) ]

    def wchan_metrics(self, gen):
        return { "wchan": "symbol{}.{}".format(self.pid, gen) }

    def root_metrics(self, gen):
        return { "root": self.root }

    def task_init_json(self):
        ev = {
                "event": "task_init_priv",
                "data": {
                    "job_id": self.job_id,
                    "task_pid": self.pid,
                    "task_global_id": self.task_rank,
                },
            }
        return json.dumps(ev)

    def task_exit_json(self):
        ev = {
                "event": "task_exit",
                "data": {
                    "job_id": self.job_id,
                    "task_pid": self.pid,
                    "task_global_id": self.task_rank,
                },
            }
        return json.dumps(ev)


pids = [ PIDSrcData(pid = 10 + r, job_id = JOB_ID, task_rank = r) \
                    for r in [0, 1] ]

def getSrcData(gen):
    return SrcData(
                reduce(lambda a,b: a+b, [pid.getSrc(gen) for pid in pids]),
                [ pid.getLDMSData(gen) for pid in pids ]
            )

src_data = [
    # 1st sample
    getSrcData(0),
    # 2nd sample
    getSrcData(1),
]

def iter_find(_fn, _iter):
    for x in _iter:
        if _fn(x):
            return x
    raise KeyError('No matching entry')

class AppSamplerTest(LDMSChrootTest, unittest.TestCase):
    CHROOT_DIR = DIR + "/chroot"
    AUTO_JOB = False
    STREAM = "slurm"

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

    def check(self, cset, lset):
        # override simple comparison between cset (from Src) and lset (from
        # LDMS) to 'extend metrics then compare' to accommodate mismatch array
        # sizes.
        self.assertEqual(cset.instance_name, lset.instance_name)
        self.assertEqual(cset.schema_name, lset.schema_name)
        c = dict(cset.metrics)
        l = dict(lset.metrics)
        # Now, we shall make sure that `c` and `l` are equivalent if we extend
        # the array in `c` to be the same size (zero-fill) as those in `l`.
        for k in c:
            vc = c[k]
            if type(vc) != tuple: # only extend tuples
                continue
            vl = l[k]
            c[k] = vc + (0,)*(len(vl) - len(vc))
        # Handle cmdline special case
        _len = c['cmdline_len']
        c_cmdline = c.pop('cmdline')
        l_cmdline = l.pop('cmdline') # still need to pop it out
        _set = iter_find(lambda x: x.instance_name_get() == cset.instance_name,
                         self.s)
        l_cmdline = _set.char_array_get(_set.metric_by_name('cmdline'))
        self.assertEqual(c_cmdline[:_len], l_cmdline[:_len])
        # Compare the rest of the metrics
        self.assertEqual(c, l)

    def test_000_0_no_job(self):
        self.ldmsDirLookup()
        self.assertEqual(self.s, [])

    def _populate_proc(self):
        # populate /proc/PIDs/
        self.updateSources(0) # This creates files according to src_data[0]
        # /proc/PID/root softlink
        for p in pids:
            path = "{}/proc/{}/root".format(self.CHROOT_DIR, p.pid)
            os.symlink('/', path)

    @classmethod
    def _cleanup(cls):
        for p in pids:
            path = "{}/proc/{}/root".format(cls.CHROOT_DIR, p.pid)
            if os.path.lexists(path):
                os.remove(path)
        # also the fd files in the 2nd sampling
        for p in pids:
            path = cls.CHROOT_DIR + "/proc/{pid}/fd/{pid}".format(pid = p.pid)
            if os.path.lexists(path):
                os.remove(path)
        super(AppSamplerTest, cls)._cleanup()

    def _send_event(self, json):
        cmd = "ldmsd_stream_publish -h localhost -x sock -p {port} -s {stream}" \
              " -t json" \
              .format( port = AppSamplerTest.PORT, stream = self.STREAM )
        proc = sp.Popen(cmd, shell = True, stdin = sp.PIPE, stdout = sp.PIPE,
                                           stderr = sp.PIPE)
        out, err = proc.communicate(json)

    def test_000_1_job_start(self):
        # populate /proc/PIDs/
        self._populate_proc()
        # publish `task_init_priv` events
        for p in pids:
            self._send_event(p.task_init_json())
        # wait a little
        time.sleep(2)
        # verify that sets existed
        self.ldmsDirLookup()
        names = set(s.instance_name_get() for s in self.s)
        e_names = set( "{}:{}/{}/{}".format(HOSTNAME, AppSamplerTest.PORT, JOB_ID,
            p.pid) for p in pids )
        self.assertEqual(names, e_names)

    def test_00_read(self):
        # update + verify data 1
        self._test(0)

    def test_01_update(self):
        # update + verify data 2
        self._test(1)

    def test_10_1_job_end(self):
        # publish `task_exit` events
        for p in pids:
            self._send_event(p.task_exit_json())
        # wait a little
        time.sleep(2)
        # verify that sets are destroyed
        self.ldmsDirLookup()
        self.assertEqual(self.s, [])


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
    DEBUG = False
    if DEBUG:
        suite = unittest.TestSuite()
        tests = filter( lambda x: x.startswith('test_'), dir(AppSamplerTest) )
        tests.sort()
        tests = [ AppSamplerTest(t) for t in tests ]
        suite.addTests(tests)
        suite.debug()
    else:
        unittest.main(failfast = True, verbosity = 2)

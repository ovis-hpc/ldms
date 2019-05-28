#!/usr/bin/env python

import os
import sys
import time
import json
import socket
import unittest
import logging

from ctypes import *
from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD

class Debug(object): pass

D = Debug()
DIR = "test_slurm"
HOSTNAME = socket.gethostname()


# ============================== #
#          ldmsd_stream          #
# ============================== #

cdll.LoadLibrary("libldmsd_stream.so")
ldmsd_stream = CDLL("libldmsd_stream.so")
ldmsd_stream.ldmsd_stream_publish.argtypes = [c_void_p, c_char_p, c_int,
                                              c_void_p, c_int64]
ldmsd_stream.ldmsd_stream_publish.restype = c_int
LDMSD_STREAM_STRING = 0
LDMSD_STREAM_JSON = 1

def ldmsd_stream_publish(x, name, data_type, data, data_len):
    rc = ldmsd_stream.ldmsd_stream_publish(int(x.this), name, data_type,
                                           data, data_len)
    if rc:
        raise RuntimeError("ldmsd_stream_publish() error: %d" % rc)

def ldmsd_stream_publish_json(x, name, obj):
    data = json.dumps(obj)
    ldmsd_stream_publish(x, name, LDMSD_STREAM_JSON, data, len(data)+1)

# Binding .publishJSON() to ldms_xprt
def x_publishJSON(self, name, obj):
    ldmsd_stream_publish_json(self, name, obj)
ldms.ldms_xprt.publishJSON = x_publishJSON

# ============================= #
#     Some helping function     #
# ============================= #

def prompt(text):
    if sys.flags.interactive:
        print
        raw_input(text)

def diff_dict(a, b):
    s0 = set(a)
    s1 = set(b)
    d0 = s0 - s1
    d1 = s1 - s0
    if d0:
        print "keys only in a:", d0
    if d1:
        print "keys only in b:", d1
    for k in s0 & s1:
        v0 = a[k]
        v1 = b[k]
        if v0 == v1:
            continue
        print k
        print "  a:", v0
        print "  b:", v1


# ================= #
#     Test Case     #
# ================= #

class SlurmTest(unittest.TestCase):
    """A test case for slurm sampler"""
    XPRT = "sock"
    SAMP_PORT = "10001"
    SAMP_LOG = DIR + "/samp.log" # set "<PATH>" to enable sampler logging
    SAMP_GDB_PORT = None # set to "20001" and remote-attach for debugging

    JOB_SIZE = 4
    TASK_SIZE = 8

    @classmethod
    def setUpClass(self):
        self.expt = None
        self.samp = None
        self.slurm_set = None # the `slurm` set
        self.mem_set = None # the `mem` (meminfo) set
        self.conn = None # ldms connection
        ldms.ldms_init(512*1024*1024) # 512MB should suffice
        try:
            cfg = """\
            load name=slurm plugin=slurm_sampler
            config name=slurm instance=slurm stream=slurm \
                              job_count=%(JOB_SIZE)d task_count=%(TASK_SIZE)d

            load name=mem plugin=meminfo
            config name=mem job_set=slurm instance=mem

            smplr_add name=mem_smplr instance=mem interval=1000000 offset=0
            smplr_start name=mem_smplr
            """ % { k: getattr(self, k) for k in dir(self) }

            self.samp = LDMSD(port=self.SAMP_PORT, cfg = cfg,
                             logfile = self.SAMP_LOG,
                             gdb_port = self.SAMP_GDB_PORT)
            D.samp = self.samp
            log.info("Starting sampler")
            self.samp.run()
            self.conn = ldms.LDMS_xprt_new(self.XPRT)
            D.conn = self.conn
            self.conn.connectByName("localhost", self.SAMP_PORT)
            self.slurm_set = self.conn.lookupSet("slurm", 0)
            D.slurm_set = self.slurm_set
            self.mem_set = self.conn.lookupSet("mem", 0)
            D.mem_set = self.mem_set
            expt = {
                    "component_id" : [0L] * self.JOB_SIZE,
                    "job_id"       : [0L] * self.JOB_SIZE,
                    "app_id"       : [0L] * self.JOB_SIZE,
                    "current_slot" :  0L,
                    "job_state"    : [0L] * self.JOB_SIZE,
                    "job_tstamp"   : [0L] * self.JOB_SIZE,
                    "job_size"     : [0L] * self.JOB_SIZE,
                    "job_uid"      : [0L] * self.JOB_SIZE,
                    "job_gid"      : [0L] * self.JOB_SIZE,
                    "job_start"    : [0L] * self.JOB_SIZE,
                    "job_end"      : [0L] * self.JOB_SIZE,
                    "node_count"   : [0L] * self.JOB_SIZE,
                    "task_count"   : [0L] * self.JOB_SIZE,
                }
            task_keys = ["task_pid", "task_rank", "task_exit_status"]
            expt.update({ "%s_%d" % (k, i) : [0L] * self.TASK_SIZE \
                                    for k in task_keys \
                                    for i in range(0, self.JOB_SIZE)
                        })
            self.expt = expt
            log.info("--- Done setting up SlurmTest ---")
        except:
            if not sys.flags.interactive:
                self.tearDownClass()
            raise

    @classmethod
    def tearDownClass(self):
        if self.slurm_set:
            del self.slurm_set
        if self.conn:
            del self.conn
        if self.samp:
            del self.samp

    def _slurm_set_verify(self):
        """Verify that self.slurm_set and self.expt are equivalent"""
        expt = { k : v if type(v) != list else tuple(v) \
                       for k,v in self.expt.iteritems() }
        D.expt = expt
        lset = self.slurm_set.as_dict()
        D.slurm_set = lset
        self.assertEqual(lset, expt)

    def _job_id_verify(self, job_id):
        """Verify that `mem_set` has the given `job_id`"""
        time.sleep(1.5) # make sure that the `mem` sampler has updated
        self.mem_set.update()
        _job_id = self.mem_set["job_id"]
        self.assertEqual(job_id, _job_id)

    def test_00_verify_schema(self):
        self._slurm_set_verify()

    def test_01_init_job10(self):
        prompt("Press ENTER to init `job10`")
        ts = int(time.time())
        job_init_data = {
                "event": "init",
                "timestamp": ts,
                "data": {
                        "job_id": 10,
                        "nnodes": 5,
                        "local_tasks": 1,
                        "uid": 1000,
                        "gid": 1000,
                        "total_tasks": 5,
                    },
            }
        self.conn.publishJSON("slurm", job_init_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt["job_start"][0] = ts
        self.expt["job_id"][0] = 10
        self.expt["job_uid"][0] = 1000
        self.expt["job_gid"][0] = 1000
        self.expt["node_count"][0] = 5
        self.expt["task_count"][0] = 1
        self.expt["job_size"][0] = 5
        self.expt["job_state"][0] = 1
        # verify
        self._slurm_set_verify()
        self._job_id_verify(10)

    def test_02_start_job10(self):
        prompt("Press ENTER to start `job10`")
        ts = int(time.time())
        task_init_data = {
                "event": "task_init_priv",
                "timestamp": ts,
                "data": {
                        "job_id": 10,
                        "task_id": 0,
                        "task_pid": 101,
                        "task_global_id": 3,
                    },
            }
        self.conn.publishJSON("slurm", task_init_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt['task_pid_0'][0] = 101
        self.expt['task_rank_0'][0] = 3
        self.expt["job_state"][0] = 2
        # verify
        self._slurm_set_verify()
        self._job_id_verify(10)

    def test_03_init_job11(self):
        prompt("Press ENTER to init `job11`")
        ts = int(time.time())
        job_init_data = {
                "event": "init",
                "timestamp": ts,
                "data": {
                        "job_id": 11,
                        "nnodes": 32,
                        "local_tasks": 2,
                        "uid": 1001,
                        "gid": 1001,
                        "total_tasks": 64,
                    },
            }
        self.conn.publishJSON("slurm", job_init_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt["current_slot"] = 1
        self.expt["job_start"][1] = ts
        self.expt["job_id"][1] = 11
        self.expt["job_uid"][1] = 1001
        self.expt["job_gid"][1] = 1001
        self.expt["node_count"][1] = 32
        self.expt["task_count"][1] = 2
        self.expt["job_size"][1] = 64
        self.expt["job_state"][1] = 1
        # verify
        self._slurm_set_verify()
        self._job_id_verify(11)

    def test_04_start_job11_task0(self):
        prompt("Press ENTER to start `job11` task0")
        ts = int(time.time())
        task_init_data = {
                "event": "task_init_priv",
                "timestamp": ts,
                "data": {
                        "job_id": 11,
                        "task_id": 0,
                        "task_pid": 1100,
                        "task_global_id": 4,
                    },
            }
        self.conn.publishJSON("slurm", task_init_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt['task_pid_1'][0] = 1100
        self.expt['task_rank_1'][0] = 4
        # verify
        self._slurm_set_verify()
        self._job_id_verify(11)

    def test_05_start_job11_task1(self):
        prompt("Press ENTER to start `job11` task1")
        ts = int(time.time())
        task_init_data = {
                "event": "task_init_priv",
                "timestamp": ts,
                "data": {
                        "job_id": 11,
                        "task_id": 1,
                        "task_pid": 1101,
                        "task_global_id": 5,
                    },
            }
        self.conn.publishJSON("slurm", task_init_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt['task_pid_1'][1] = 1101
        self.expt['task_rank_1'][1] = 5
        self.expt["job_state"][1] = 2
        # verify
        self._slurm_set_verify()
        self._job_id_verify(11)

    def test_06_job10_task0_exit(self):
        prompt("Press ENTER to exit task0 on job10")
        ts = int(time.time())
        task_exit_data = {
                "event": "task_exit",
                "timestamp": ts,
                "data": {
                        "job_id": 10,
                        "task_id": 0,
                        "task_exit_status": 2,
                    },
            }
        self.conn.publishJSON("slurm", task_exit_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt["job_state"][0] = 3
        self.expt["task_exit_status_0"][0] = 2
        # verify
        self._slurm_set_verify()
        self._job_id_verify(11)

    def test_07_job10_exit(self):
        prompt("Press ENTER to exit job10")
        ts = int(time.time())
        job_exit_data = {
                "event": "exit",
                "timestamp": ts,
                "data": {
                        "job_id": 10,
                    },
            }
        self.conn.publishJSON("slurm", job_exit_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt["job_state"][0] = 4
        self.expt["job_end"][0] = ts
        # verify
        self._slurm_set_verify()
        self._job_id_verify(11)

    def test_08_job11_task0_exit(self):
        prompt("Press ENTER to exit task0 on job11")
        ts = int(time.time())
        task_exit_data = {
                "event": "task_exit",
                "timestamp": ts,
                "data": {
                        "job_id": 11,
                        "task_id": 0,
                        "task_exit_status": 22,
                    },
            }
        self.conn.publishJSON("slurm", task_exit_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt["job_state"][1] = 3
        self.expt["task_exit_status_1"][0] = 22
        # verify
        self._slurm_set_verify()
        self._job_id_verify(11)

    def test_09_job11_task1_exit(self):
        prompt("Press ENTER to exit task1 on job11")
        ts = int(time.time())
        task_exit_data = {
                "event": "task_exit",
                "timestamp": ts,
                "data": {
                        "job_id": 11,
                        "task_id": 1,
                        "task_exit_status": 22,
                    },
            }
        self.conn.publishJSON("slurm", task_exit_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt["job_state"][1] = 3
        self.expt["task_exit_status_1"][1] = 22
        # verify
        self._slurm_set_verify()
        self._job_id_verify(11)

    def test_10_job11_exit(self):
        prompt("Press ENTER to exit job11")
        ts = int(time.time())
        job_exit_data = {
                "event": "exit",
                "timestamp": ts,
                "data": {
                        "job_id": 11,
                    },
            }
        self.conn.publishJSON("slurm", job_exit_data)
        time.sleep(0.2)
        # update lset
        self.slurm_set.update()
        # update expt
        self.expt["job_state"][1] = 4
        self.expt["job_end"][1] = ts
        # verify
        self._slurm_set_verify()
        self._job_id_verify(0)


if __name__ == "__main__":
    pystart = os.getenv("PYTHONSTARTUP")
    if pystart:
        execfile(pystart)
    if not os.path.exists(DIR):
        os.makedirs(DIR)
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = DIR + "/test_meminfo.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    # unittest.TestLoader.testMethodPrefix = 'test_'
    unittest.main(failfast = True, verbosity = 2)

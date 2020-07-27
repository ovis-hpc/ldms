#!/usr/bin/python3

# Copyright (c) 2018 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
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

# Test LDMSD with ldms_auth_munge library
# Prerequisite:
#   - munged, munge
#   - run as root

from builtins import range
from builtins import object
import re
import os
import pdb
import pwd
import sys
import time
import shutil
import logging
import unittest
from subprocess import Popen, PIPE, STDOUT, DEVNULL

from distutils.spawn import find_executable

from ovis_ldms import ldms
from ldmsd.ldmsd_util import LDMSD
from ldmsd.ldmsd_config import ldmsdInbandConfig
from ldmsd.ldmsd_request import LDMSD_Request

# log = logging.getLogger(__name__)

log = None

class Debug(object): pass

DEBUG = Debug()

ldms.init(512*1024*1024) # 512MB should suffice

DIR = "%s/munge_dir" % (sys.path[0] if sys.path[0] else  os.getcwd())
shutil.rmtree(DIR, ignore_errors = True)
os.makedirs(DIR, mode=0o755)

class Munged(object):
    """Munge Daemon"""
    def __init__(self, socket=None, log=None, uid=None, gid=None,
                 keyfile=None, key=None, pidfile=None):
        self.exe = find_executable("munged")
        if not self.exe:
            raise RuntimeError("munged not found")
        self.socket = socket if socket else DIR + "/munge_sock"
        self.log = log if log else DIR + "/munge_log"
        self.keyfile = keyfile if keyfile else DIR + "/munge_key"
        #self.pidfile = pidfile if pidfile else DIR + "/munge_pid"
        self.uid = uid if uid else os.getuid()
        self.gid = gid if gid else os.getgid()
        self.proc = None
        if key:
            self.write_keyfile(key)

    def write_keyfile(self, key):
        with open(self.keyfile, "w") as f:
            f.write(key)
        os.chmod(self.keyfile, 0o600)
        os.chown(self.keyfile, self.uid, self.gid)

    def _preexec(self):
        os.setgid(self.gid)
        os.setuid(self.uid)

    def start(self):
        if self.proc:
            raise RuntimeError("Process has already started.")
        args = [
            self.exe,
            '-F',
            '-f',
            '-S', self.socket,
            '--key-file', self.keyfile,
            #'--pid-file', self.pidfile,
        ]
        self.proc = Popen(args,
                          stdin = DEVNULL,
                          stdout = DEVNULL,
                          stderr = STDOUT,
                          preexec_fn = self._preexec)

    def terminate(self, wait = True):
        if not self.proc:
            raise RuntimeError("Process not started")
        self.proc.terminate()
        if wait:
            self.proc.wait()
        self.proc = None

    def __del__(self):
        if self.proc:
            self.terminate()


def ldms_ls(xprt, port, auth, auth_opt=None, uid=os.getuid(), gid=os.getgid()):
    """ldms_ls using munge"""
    exe = find_executable('ldms_ls')
    if not exe:
        raise RuntimeError("ldms_ls not found")
    args = [
        exe,
        '-h', 'localhost',
        '-x', xprt,
        '-p', port,
        '-a', auth,
    ]
    if auth_opt:
        for k, v in auth_opt.items():
            args.extend(['-A', '%s=%s' % (k, v)])
    def _px():
        os.setgid(gid)
        os.setuid(uid)
    p = Popen(args, stdin=PIPE, stdout=PIPE, stderr=STDOUT, preexec_fn=_px)
    sout, serr = p.communicate()
    return sout.decode().splitlines()

class TestLDMSAuthMunge(unittest.TestCase):
    XPRT = "sock"
    PORT = "10201"
    AUTH = "munge"

    @classmethod
    def setUpClass(cls):
        if os.getuid() != 0 or os.getgid() != 0:
            raise RuntimeError("This test needs to run as `root`, "
                               "suggestion: `sudo -E bash` to become root "
                               "with your current environments, then execute "
                               "this script.")
        usr = pwd.getpwall()[1]
        cls.usr = usr
        cls.munged = None
        cls.ldmsd = None
        try:
            log.info("--- Setting up %s ---" % cls.__name__)

            cls.munged = Munged(key = ''.join ('0' for i in range(0, 1024)))
            cls.munged.start()

            # ldmsd sampler conf
            cfg = """\
            load name=meminfo
            config name=meminfo producer=smp instance=smp/meminfo \
                                schema=meminfo component_id=1 \
                                uid=0 gid=0 perm=0600
            start name=meminfo interval=1000000
            load name=vmstat
            config name=vmstat producer=smp instance=smp/vmstat \
                               schema=vmstat component_id=1 \
                               uid=%(usr_uid)d gid=%(usr_gid)d perm=0600
            start name=vmstat interval=1000000
            """ % {
                'usr_uid': usr.pw_uid,
                'usr_gid': usr.pw_gid,
            }

            cls.AUTH_OPT = {"socket": cls.munged.socket}
            cls.ldmsd = LDMSD(port=cls.PORT, cfg = cfg,
                              auth=cls.AUTH, auth_opt = cls.AUTH_OPT,
                              logfile=DIR+"/ldmsd.log")
            log.info("starting ldmsd")
            cls.ldmsd.run()
            time.sleep(1)
            log.info("--- Done setting up %s ---" % cls.__name__)
        except Exception as e:
            del cls.ldmsd
            del cls.munged
            raise

    @classmethod
    def tearDownClass(cls):
        log.info("--- Tearing down TestLDMSAuthOvis ---")
        del cls.munged
        del cls.ldmsd

    def test_01(self):
        """ldms_ls as root, expect root-owned dir"""
        dirs = ldms_ls(self.XPRT, self.PORT, self.AUTH, self.AUTH_OPT)
        _dirs = []
        for d in dirs:
            _dirs.append(d)
        self.assertEqual(_dirs, ['smp/vmstat', 'smp/meminfo'])

    def test_02(self):
        """ldms_ls as user#1, expect user#1-owned dir"""
        dirs = ldms_ls(self.XPRT, self.PORT, self.AUTH, self.AUTH_OPT,
                       uid = self.usr.pw_uid,
                       gid = self.usr.pw_gid)
        self.assertEqual(dirs, ['smp/vmstat'])

    def test_03(self):
        """ldms_ls as other user, expect no dirs"""
        usr = pwd.getpwall()[2]
        dirs = ldms_ls(self.XPRT, self.PORT, self.AUTH, self.AUTH_OPT,
                       uid = usr.pw_uid,
                       gid = usr.pw_gid)
        self.assertEqual(dirs, [])

if __name__ == "__main__":
    fmt = "%(asctime)s.%(msecs)d %(levelname)s: %(message)s"
    datefmt = "%F %T"
    logging.basicConfig(
            format = fmt,
            datefmt = datefmt,
            level = logging.DEBUG,
            filename = "ldmsd_auth_ovis.log",
            filemode = "w",
    )
    log = logging.getLogger(__name__)
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(logging.Formatter(fmt, datefmt))
    log.addHandler(ch)
    unittest.main(failfast = True, verbosity = 2)

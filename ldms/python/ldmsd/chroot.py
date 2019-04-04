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
import json
import shutil
import socket
import logging
import unittest
import threading
import subprocess as sp

from StringIO import StringIO
from collections import namedtuple

from ovis_ldms import ldms
from ldmsd_util import LDMSD
from ldmsd_config import ldmsdInbandConfig
from ldmsd_request import LDMSD_Request

from distutils.spawn import find_executable as which

class Debug(object): pass
D = Debug()

ldms.ldms_init(64*1024*1024)

Src = namedtuple('Src', ['path', 'content'])
SrcData = namedtuple('SrcData', ['src', 'data'])
# SrcData.src is the Src (or list of Src) describing file(s) and the contents
# SrcData.data is the LDMSData corresponding to the source

def prog_to_prefix(prog_name):
    path = which(prog_name)
    (prefix, bindir, pname) = path.rsplit('/', 2)
    assert(bindir in ["bin", "sbin"])
    return prefix

LDMS_PREFIX = prog_to_prefix("ldms-pedigree")
OVIS_LIB_PREFIX = prog_to_prefix("lib-pedigree")

def xvars(obj):
    keys = dir(obj)
    return { k: getattr(obj, k) for k in keys }

def call(*args):
    p = sp.Popen(args, stdout=sp.PIPE, stderr=sp.PIPE)
    (out, err) = p.communicate()
    return (out, err, p.returncode)

def touch(path):
    with open(path, 'a'):
        os.utime(path, None)

def mount(dev, mp, *args):
    (out, err, rc) = call("/bin/mount", *(args + (dev, mp)))
    if rc:
        D.mount_args = [dev, mp, args]
        raise RuntimeError("mount failed, rc: %d, err: %s" % (rc, err))

def umount(mp):
    call("/bin/umount", mp)

def env_path_add(env, var, path):
    """Add a path into `var` environmental variable"""
    v = env.get(var, "")
    paths = v.split(':')
    if path not in paths:
        paths.append(path)
    v = ':'.join(paths)
    env[var] = v

def set_prog_env(env, prefix):
    env_path_add(env, "PATH", prefix + "/bin")
    env_path_add(env, "PATH", prefix + "/sbin")
    env_path_add(env, "LD_LIBRARY_PATH", prefix + "/lib")
    ver = "%d.%d" % (sys.version_info.major, sys.version_info.minor)
    path = prefix + "/lib/python" + ver + "/site-packages"
    env_path_add(env, "PYTHONPATH", path)

def set_ldms_env(env, prefix):
    set_prog_env(env, prefix)
    env["LDMSD_PLUGIN_LIBPATH"] = prefix + "/lib/ovis-ldms"

def set_ovis_lib_env(env, prefix):
    set_prog_env(env, prefix)
    env["ZAP_LIBPATH"] = prefix + "/lib/ovis-lib"

class MountBinder(object):
    """Manage `mount -o ro,bind` directory

    mb = MountBinder(dst, src)

    dst directory must not exist as it is created and removed by MountBinder.

    """
    def __init__(self, dst, src):
        self.dst = dst
        self.src = src
        self.state = 0 # 0: init, 1: mkdir OK, 2: mount OK

    def mkdir(self):
        if self.state != 0:
            D.mb = self
            raise RuntimeError("Bad mount binder state: %d" % self.state)
        os.makedirs(self.dst)
        self.state = 1

    def mount(self):
        if self.state == 0:
            # conveniently call mkdir if not yet done so
            self.mkdir()
        if self.state != 1:
            D.mb = self
            raise RuntimeError("Bad mount binder state: %d" % self.state)
        mount(self.src, self.dst, "-o", "bind,ro")
        self.state = 2

    def umount(self):
        if self.state != 2:
            D.mb = self
            raise RuntimeError("Bad mount binder state: %d" % self.state)
        umount(self.dst)
        self.state = 1

    def rmdir(self):
        if self.state != 1:
            D.mb = self
            raise RuntimeError("Bad mount binder state: %d" % self.state)
        os.removedirs(self.dst)

    def __del__(self):
        if self.state == 2:
            self.umount()
        if self.state == 1:
            self.rmdir()


class Chrooter(object):
    """A Chrooter utility

    Synopsis:
        ```
        ch = Chrooter(chroot_dir [, mount_map] [, extra_map])
        ch.prep()
        ch.cleanup()
        ```

    Constructor parameters:
        chroot_dir - the new root directory.
        mount_map - the { DIR_IN_CHROOT : ACTUAL_PATH } dictionary of
                    directories. If not specified, default to MOUNT_MAP which
                    contains `/bin`, `/lib`, `/sbin`, `/usr`, and `/tmp`.
        extra_map - additional { DIR_IN_CHROOT : ACTUAL_PATH } dictionary. This
                    is useful to specify only additional directories that build
                    on top of default `mount_map`. Otherwise, appropriate
                    mount_map has to be constructed.

    Chrooter.prep() creates the `chroot_dir` directory, as well as directories
    in `mount_map` and `extra_map`, and then `mount -o bind,ro` the `mount_map`
    and `extra_map`. `<chroot_dir>/THIS_IS_CHROOT` is also created so that the
    program running under chroot can deterine whether it really is running under
    a chrooted environment (by checking `/THIS_IS_CHROOT` path).

    Chrooter.cleanup() reverse the Chrooter.prep(). Please note that the files
    in `chroot_dir/` not written by Chrooter won't be deleted. If there are
    files left hanging in `chroot_dir/`, the cleanup will fail. The program
    running under chroot should clean themselves up before Chrooter.cleanup() is
    called.

    """

    CHROOT_FILE = "/THIS_IS_CHROOT"
    # default mount_map
    MOUNT_MAP = {
        # DIR_IN_CHROOT : ACTUAL_PATH
        "/bin"   : "/bin",
        "/lib"   : "/lib",
        "/lib64"   : "/lib64",
        "/sbin"  : "/sbin",
        "/usr"   : "/usr",
        "/tmp"   : "/tmp",
    }

    def __init__(self, chroot_dir, mount_map = MOUNT_MAP, extra_map = None):
        self.root = chroot_dir
        self.mount_map = mount_map.copy()
        if extra_map:
            self.mount_map.update(extra_map)
        self.mbs = []

    def __del__(self):
        self.cleanup()

    def prep(self):
        os.mkdir(self.root)
        touch(self.root + self.CHROOT_FILE)
        for k, src in self.mount_map.iteritems():
            dst = self.root + k
            mb = MountBinder(dst, src)
            mb.mount()
            self.mbs.append(mb)

    def cleanup(self):
        while self.mbs:
            mb = self.mbs.pop()
            del mb
        f = self.root + self.CHROOT_FILE
        if os.path.exists(f):
            os.remove(f)
        if os.path.exists(self.root):
            os.rmdir(self.root)

    def run(self, prog_args, env=None):
        """run a program under chroot

        Examples:
            ch.run(["/bin/ls", "-l"])
            ch.run(["/bin/ls", "-l"], { "PATH": "/bin:/sbin" })
        """
        _a = ["chroot", self.root]
        _a.extend(prog_args)
        proc = sp.Popen(_a, stdout=sp.PIPE, stderr=sp.PIPE, env=env)
        (out, err) = proc.communicate()
        return (out, err)


def copy_proc(proc_path, chroot):
    """copy a proc file {proc_path} to {chroot}/{proc_path}"""
    dst = chroot + proc_path
    d = os.path.dirname(dst)
    os.makedirs(d)
    fin = open(proc_path)
    fout = open(dst, "w")
    fout.write(fin.read())
    fin.close()
    fout.close()

def rm_proc(proc_path, chroot):
    """remove the fake proc, also remove empty parent directories"""
    x = chroot + proc_path
    os.remove(x)
    (x, _) = x.rsplit('/', 1)
    while x > chroot:
        try:
            os.rmdir(x)
            (x, _) = x.rsplit('/', 1)
        except:
            break

def ldms_try_connect(host, port, xprt = "sock", timeout = 1):
    t0 = t = time.time()
    while t - t0 < timeout:
        x = ldms.LDMS_xprt_new(xprt)
        try:
            x.connectByName(str(host), str(port))
        except:
            pass # just try again ...
        else:
            # connect success
            return x
        t = time.time()
    raise RuntimeError("ldms_try_connect timeout (%d sec)" % timeout)

def ldms_try_dir(x, timeout = 1):
    # keep doing dir until we have some set back
    t0 = t = time.time()
    while t - t0 < timeout:
        d = x.listDir()
        if d:
            return d
        t = time.time()
    raise RuntimeError("ldms_try_dir timeout (%d sec)" % timeout)

def ldms_steady_dir(x, timeout = 10, between = 1):
    t0 = t = time.time()
    d0 = ldms_try_dir(x, between)
    while t - t0 < timeout:
        time.sleep(between)
        d1 = ldms_try_dir(x, between)
        if d0 == d1:
            return d0
        d0 = d1
    raise RuntimeError("ldms_steady_dir timeout (%d sec)" % timeout)

class LDMSData(object):
    """LDMS data representation"""
    __slots__ = ["instance_name", "schema_name", "metrics"]
    def __init__(self, obj):
        """Create LDMSData from either `ldms_set` or `dict`"""
        if type(obj) == dict:
            self._initFromDict(obj)
        elif type(obj) == ldms.ldms_rbuf_desc:
            self._initFromLdmsSet(obj)
        elif type(obj) == LDMSData:
            self._copy(obj)
        else:
            raise ValueError("Unknown type to convert to LDMSData")

    def _initFromDict(self, d):
        self.instance_name = d["instance_name"]
        self.schema_name = d["schema_name"]
        self.metrics = frozenset(d["metrics"].iteritems())

    def _initFromLdmsSet(self, s):
        self.instance_name = s.instance_name_get()
        self.schema_name = s.schema_name_get()
        self.metrics = frozenset(s.as_dict().iteritems())

    def _copy(self, other):
        self.instance_name = other.instance_name
        self.schema_name = other.schema_name
        self.metrics = other.metrics # OK as `metrics` is immutable

    def __str__(self):
        sio = StringIO()
        print >>sio, "--------"
        print >>sio, "  instance_name :", self.instance_name
        print >>sio, "  schema_name :", self.schema_name
        print >>sio, "  metrics :"
        for k, v in self.metrics:
            print >>sio, "   ", k, ":", v
        print >>sio, "--------"
        return sio.getvalue()

    def __eq__(self, other):
        return self.instance_name == other.instance_name and \
               self.schema_name == other.schema_name and \
               self.metrics == other.metrics

    def __hash__(self):
        return hash(self.instance_name) * hash(self.schema_name) * \
               hash(self.metrics)


class LDMSChrootTest(object):
    """LDMSChrootTest - a common routine for LDMS chroot-based test cases."""
    XPRT = "sock"
    PORT = "10001"
    LOG = None # chrooted path
    COMPONENT_ID = 11
    AUTO_JOB = True
    JOB_ID = 22
    APP_ID = 33
    INTERVAL_SEC = 1
    INTERVAL_USEC = INTERVAL_SEC * 1000000
    OFFSET = 0
    CHROOT_DIR = "chroot"
    LDMSD_LOG = "/ldmsd.log" # under chroot
    HOSTNAME = socket.gethostname()
    SET_PREFIX = HOSTNAME + ":" + PORT + "/"

    smp = None # the sampler daemon
    x = None # the xprt
    d = None # the directory list
    s = None # the list of ldms_set

    ch = None # Chrooter
    env = None # Chroot Environment

    @classmethod
    def getPluginName(cls):
        """Return `str` plugin name"""
        raise NotImplementedError("Please overrride getPluginName()")

    @classmethod
    def getPluginParams(cls):
        """Returns dict(NAME:VAL) for extra plugin parameters"""
        return dict()

    @classmethod
    def getLdmsConfig(cls):
        D.cls = cls
        cls.PLUGIN_NAME = cls.getPluginName()
        p = cls.getPluginParams()
        cls.PLUGIN_PARAMS = ' '.join([ str(k)+'='+str(v) \
                                          for k,v in p.iteritems() ])
        D.s = s = """
            load name=test plugin=%(PLUGIN_NAME)s
            config name=test component_id=%(COMPONENT_ID)d %(PLUGIN_PARAMS)s
            smplr_add name=smplr_test plugin=test instance=NA producer=NA \
                      component_id=0
            smplr_start name=smplr_test interval=%(INTERVAL_USEC)d \
                        offset=%(OFFSET)d
        """
        return s % xvars(cls)

    @classmethod
    def getSrcData(cls, tidx):
        """Return `SrcData` for time index `tidx`"""
        raise NotImplementedError("Please overrride getLdmsConfig()")

    @classmethod
    def initSources(cls):
        """Perform metric source file(s) initialization"""
        cls.updateSources(0)

    @classmethod
    def updateSources(cls, tidx):
        """Perform metric source file(s) update"""
        sd = cls.getSrcData(tidx)
        srcs = [ sd.src ] if type(sd.src) == Src else sd.src
        for src in srcs:
            path = cls.CHROOT_DIR + src.path
            d = os.path.dirname(path)
            if not os.path.exists(d):
                os.makedirs(d)
            pf = open(path, "w")
            content = src.content
            if type(content) == str:
                content = [ content ]
            if type(content) == file:
                content.seek(0)
            for s in content:
                pf.write(s)
            pf.close()

    @classmethod
    def cleanUpSources(cls):
        """Clean up the sources"""
        sd = cls.getSrcData(0)
        srcs = [ sd.src ] if type(sd.src) == Src else sd.src
        for src in srcs:
            path = cls.CHROOT_DIR + src.path
            os.remove(path)
            d = os.path.dirname(path)
            if not os.listdir(d): # dir empty
                os.removedirs(d)

    @classmethod
    def setUpClass(cls):
        try:
            # prep chroot first
            extra = {
                LDMS_PREFIX     : LDMS_PREFIX,
                OVIS_LIB_PREFIX : OVIS_LIB_PREFIX,
            }
            cls.ch = Chrooter(cls.CHROOT_DIR, extra_map = extra)
            cls.ch.prep()

            # prep env
            cls.env = { "PATH": "/bin:/sbin:/usr/bin:/usr/sbin:"
                                 "/usr/local/bin:/usr/local/sbin" }
            set_ldms_env(cls.env, LDMS_PREFIX)
            set_ovis_lib_env(cls.env, OVIS_LIB_PREFIX)

            # ldmsd
            cfg = '' if not cls.AUTO_JOB else """\
                load name=jobinfo plugin=faux_job
                config name=jobinfo job_id=%(JOB_ID)d app_id=%(APP_ID)d
                smplr_add name=smplr_job plugin=jobinfo \
                          producer=NA instance=NA component_id=NA
                smplr_start name=smplr_job interval=3600000000
            """ % xvars(cls)
            cls.initSources()
            cfg += cls.getLdmsConfig()
            cls.smp = LDMSD(port = cls.PORT, cfg = cfg,
                            logfile = cls.LDMSD_LOG,
                            chroot = cls.CHROOT_DIR, env = cls.env)
            cls.smp.run()
            cls.x = ldms_try_connect("localhost", cls.PORT, cls.XPRT, 4)
            cls.d = ldms_steady_dir(cls.x)
            cls.s = list()
            for name in cls.d:
                _s = cls.x.lookupSet(name, 0)
                cls.s.append(_s)
            if sys.flags.interactive:
                raw_input("Press ENTER to continue")
        except:
            if not sys.flags.interactive:
                cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        cls._cleanup()

    @classmethod
    def _cleanup(cls):
        if sys.flags.interactive:
            raw_input("Press ENTER to clean up ...")
        if cls.s:
            del cls.s
        if cls.x:
            del cls.x
        if cls.smp:
            del cls.smp
        logfile = cls.CHROOT_DIR + cls.LDMSD_LOG if cls.LDMSD_LOG else None
        if logfile and os.path.exists(logfile):
            os.remove(logfile)
        if cls.ch:
            cls.cleanUpSources()
            del cls.ch

    def ldmsUpdate(self):
        """Update all ldms sets in `self.s`, return them as set of LDMSData"""
        for _s in self.s:
            _s.update()
        return list( LDMSData(_s) for _s in self.s )

    def _cdataModify(self, cdata):
        # make sure that cdata has component_id, job_id and app_id
        if not self.AUTO_JOB:
            return
        assert(type(cdata) == list)
        inc = [ ("component_id", self.COMPONENT_ID),
                ("job_id", self.JOB_ID),
                ("app_id", self.APP_ID) ]
        for x in cdata:
            x.metrics = x.metrics.union(inc)

    def check(self, cset, lset):
        self.assertEqual(cset, lset)

    def _test(self, tidx):
        self.updateSources(tidx)
        time.sleep(self.INTERVAL_SEC)
        sd = self.getSrcData(tidx)
        cdata = sd.data
        if type(cdata) == LDMSData:
            cdata = [ cdata ]
        self._cdataModify(cdata)
        ldata = self.ldmsUpdate()
        self.assertGreater(len(cdata), 0)
        cdata = { x.instance_name: x for x in cdata }
        ldata = { x.instance_name: x for x in ldata }
        D.cdata = cdata.copy()
        D.ldata = ldata.copy()
        keys = cdata.keys()
        for name in keys:
            cset = cdata.pop(name)
            lset = ldata.pop(name)
            self.check(cset, lset)
        if self.AUTO_JOB:
            ldata.pop(self.SET_PREFIX + "jobinfo")
        self.assertEqual(len(ldata), 0,
                         "Unexpected LDMS sets: " + str(ldata.keys()))

    def test_00_read(self):
        self._test(0)

    def test_01_update(self):
        self._test(1)


def try_sudo():
    uid = os.getuid()
    if uid:
        print "uid:", uid
        print "Escalating as root to run this script"
        envs = ["PATH", "PYTHONPATH", "LD_LIBRARY_PATH", "PYTHONSTARTUP",
                "LDMSD_PLUGIN_LIBPATH", "ZAP_LIBPATH"]

        envs = [ "%s=%s" % (e, os.getenv(e, '')) for e in envs ]

        args = ["/usr/bin/sudo"]
        args.extend(envs)
        args.append(sys.executable)
        if sys.flags.interactive:
            args.append("-i")
        args.extend(sys.argv)
        os.execl(args[0], *args)

if __name__ == "__main__":
    # for tab completion
    import rlcompleter, readline
    readline.parse_and_bind('tab: complete')
    try_sudo()
    extra = {
        LDMS_PREFIX     : LDMS_PREFIX,
        OVIS_LIB_PREFIX : OVIS_LIB_PREFIX,
    }
    CHROOT_DIR = "chroot"
    ch = Chrooter(CHROOT_DIR, extra_map = extra)
    ch.prep()

    env = {"PATH": "/bin:/sbin:/usr/bin:/usr/sbin:"
                   "/usr/local/bin:/usr/local/sbin"}
    set_ldms_env(env, LDMS_PREFIX)
    set_ovis_lib_env(env, OVIS_LIB_PREFIX)

    copy_proc("/proc/meminfo", CHROOT_DIR)

    smpcfg = """
        load name=mem plugin=meminfo
        config name=mem interval=1000000 offset=0 component_id=100
        start name=mem
    """
    smp = LDMSD(port = "10001", cfg = smpcfg,
                # logfile = "/ldmsd.log",
                chroot=CHROOT_DIR, env=env)
    smp.run()

    raw_input("Press ENTER to terminate")
    del(smp)
    rm_proc("/proc/meminfo", CHROOT_DIR)

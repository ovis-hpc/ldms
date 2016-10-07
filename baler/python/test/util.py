#!/usr/bin/env python
import logging
import re
import os
import sys
import subprocess
import time
import abhttp
from StringIO import StringIO
from datetime import datetime

logger = logging.getLogger(__name__)

class Param(dict):
    __getattr__ = dict.get
    __setattr__ = dict.__setitem__
    __delattr__ = dict.__delitem__

gparam = Param(
        N_DAEMONS            =  4,
        N_PATTERNS           =  8,
        TS_BEGIN             =  1451628000,
        TS_LEN               =  3600*2,
        TS_INC               =  1800,
        NODE_BEGIN           =  0,
        NODE_LEN             =  64,
        TMP_DIR              =  "./tmp",
        SYSLOG_PORT_START    =  30000,
        MASTER_PORT_START    =  31000,
        HTTP_PORT_START      =  18000,
        HTTP_BAK_PORT_START  =  18800,
        LEVEL                =  "INFO",
        HOSTSFILE            =  "hosts",
        BHTTPD_MAIN          =  True,
        BHTTPD_BAK           =  False,
)


num_text = {
    0: "Zero",
    1: "One",
    2: "Two",
    3: "Three",
    4: "Four",
    5: "Five",
    6: "Six",
    7: "Seven",
    8: "Eight",
    9: "Nine",
}

num = {
    "Zero":   0,
    "One":    1,
    "Two":    2,
    "Three":  3,
    "Four":   4,
    "Five":   5,
    "Six":    6,
    "Seven":  7,
    "Eight":  8,
    "Nine":   9,
}

def num2text(num):
    s = StringIO()
    dirty = False
    unit = 1000000
    while (unit):
        x = int(num / unit) % 10
        if (x or dirty):
            if dirty:
                s.write(" ")
            s.write(str(num_text[x]))
            dirty = True
        unit /= 10
    if not dirty:
        s.write(str(num_text[0]))
    return s.getvalue()


class Balerd(object):
    """Balerd control for testing."""
    # balerd will run in TMP_DIR
    CONFIG_TEMPLATE="""
tokens type=ENG path=test_root/eng-dictionary
tokens type=HOST path=test_root/hosts
plugin name=bout_sos_img delta_ts=3600
plugin name=bout_sos_img delta_ts=60
plugin name=bout_sos_msg
plugin name=bin_rsyslog_tcp port=%(syslog_port)d
"""

    def __init__(self, iD, tmp_dir, param={}):
        global gparam
        uparam = param
        param = Param(gparam)
        param.update(uparam)
        self.param = param
        self.iD = iD
        self.syslog_port = param.SYSLOG_PORT_START + iD
        self.master_port = param.MASTER_PORT_START + iD
        self.name = "balerd.%d" % iD
        self.store = "store.%d" % iD
        self.log = "balerd.log.%d" % iD
        self.cfg = "config.%d" % iD
        self.level = param.LEVEL
        self.proc = None
        self.tmp_dir = tmp_dir
        self.cmd = """
        mkdir -p %(tmp_dir)s && cd %(tmp_dir)s || exit -1
        ln -fs %(test_root)s test_root
        X=$(which balerd)
        if test -z "$X"; then
            echo "balerd not found!"
            exit -1
        fi
        ln -f -s $X %(name)s
        exit_hook() {
            pkill %(name)s
        }
        trap 'exit_hook' EXIT
        ./%(name)s -F -s %(store)s -l %(log)s -C %(cfg)s -v %(level)s \
                         -p %(master_port)d
        """ % {
            "tmp_dir": self.tmp_dir,
            "name": self.name,
            "store": self.store,
            "log": self.log,
            "cfg": self.cfg,
            "level": self.level,
            "master_port": self.master_port,
            "syslog_port": self.syslog_port,
            "test_root": os.getcwd(),
        }
        pass

    def cleanup(self):
        subprocess.call("""
        test -d %(tmp_dir)s || exit 0
        cd %(tmp_dir)s
        test -d %(store)s && test %(store)s != "/" && rm -rf %(store)s
        """ % {
            "tmp_dir": self.tmp_dir,
            "store": self.store,
        }, shell=True)

    def start(self):
        # create config file first
        logger.info("Starting baler daemon(%d)", self.iD)
        if self.proc and self.proc.poll() == None:
            raise RuntimeError("'%s' is running.")
        if not os.path.isdir(self.tmp_dir):
            os.makedirs(self.tmp_dir, mode=0755)
        out = open(self.tmp_dir + "/" + self.cfg, "w")
        out.write(self.CONFIG_TEMPLATE % {"syslog_port": self.syslog_port})
        out.close()
        self.proc = subprocess.Popen(self.cmd, shell=True,
                                     executable="/bin/bash")

    def term(self):
        logger.info("Stopping baler daemon(%d)", self.iD)
        self.proc.terminate()

    pass
######## end of class Balerd ########


class Bhttpd(object):
    """Baler HTTPD control for testing."""

    def __init__(self, iD, tmp_dir, bak=False, param={}):
        global gparam
        uparam = param
        param = Param(gparam)
        param.update(uparam)
        self.param = param
        self.iD = iD
        self.store = "store.%d" % iD
        if bak:
            self.name = "bhttpd.bak.%d" % iD
            self.log = "bhttpd.bak.log.%d" % iD
            self.port = param.HTTP_BAK_PORT_START + iD
        else:
            self.name = "bhttpd.%d" % iD
            self.log = "bhttpd.log.%d" % iD
            self.port = param.HTTP_PORT_START + iD
        self.proc = None
        self.cmd = """
        mkdir -p %(tmp_dir)s && cd %(tmp_dir)s || exit -1
        X=$(which bhttpd)
        if test -z "$X"; then
            echo "Cannot find bhttpd"
            exit -1
        fi
        ln -f -s $X %(name)s || exit -1
        exit_hook() {
            pkill %(name)s
        }
        trap 'exit_hook' EXIT
        ./%(name)s -F -s %(store)s -v %(level)s -p %(port)s >>%(log)s 2>&1
        """ % {
            "tmp_dir": tmp_dir,
            "name": self.name,
            "store": self.store,
            "log": self.log,
            "level": param.LEVEL,
            "port": self.port,
        }

    def start(self):
        logger.info("Starting bhttpd(%d)", self.iD)
        self.proc = subprocess.Popen(self.cmd, shell=True,
                                     executable="/bin/bash")

    def term(self):
        logger.info("Stopping bhttpd(%d)", self.iD)
        self.proc.terminate()
######## end of class Bhttpd ########


def host_name(i):
    return "node%05d" % i


host_ptn = re.compile("node(\d+)")
def host_id(name):
    return int(host_ptn.match(name).group(1))


def gen_ptn_template(ptn_id):
    return "This is pattern %s: %%d" % num2text(ptn_id)


class MsgGenIter(object):

    def __init__(self, uhost_ids=None, uptn_ids=None, ts0=None, ts1=None, param={}):
        global gparam
        uparam = param
        param = Param(gparam)
        param.update(uparam)

        self.PTN_TEMPLATE = []
        self.param = param
        ts_inc = param.TS_INC
        ts_begin = param.TS_BEGIN
        ts_len = param.TS_LEN
        node_begin = param.NODE_BEGIN
        node_len = param.NODE_LEN
        n_daemons = param.N_DAEMONS
        n_patterns = param.N_PATTERNS

        for i in range(0, param.N_PATTERNS):
            template = gen_ptn_template(i)
            self.PTN_TEMPLATE.append(template)

        if not uhost_ids:
            uhost_ids = "%d-%d" % (node_begin, node_begin + node_len -1)
        if not uptn_ids:
            uptn_ids = "%d-%d" % (0, n_patterns - 1)
        if ts0 == None:
            ts0 = ts_begin
        if ts1 == None:
            ts1 = ts_begin + ts_len

        hids = abhttp.IDSet()
        hids.add_smart(uhost_ids)
        hids = [x for x in hids]
        hids.sort()
        self.host_ids = hids
        logger.debug("host_ids: %s", self.host_ids)

        pids = abhttp.IDSet()
        pids.add_smart(uptn_ids)
        pids = [x for x in pids]
        pids.sort()
        self.ptn_ids = pids
        logger.debug("ptn_ids: %s", self.ptn_ids)

        # round-up for ts0
        self.ts0 = int((ts0 + ts_inc - 1)/ts_inc)*ts_inc
        self.ts1 = ts1

        logger.debug("MsgGenIter - ts0: %d, ts1: %d", self.ts0, self.ts1)

        self.curr_ts = self.ts0
        self.ts_inc = ts_inc

        self.hitr = iter(self.host_ids)

        self.pitr = iter(self.ptn_ids)
        self.curr_ptn_id = next(self.pitr)

        self.n_daemons = n_daemons
        pass

    def msg_text(self, ptn_id, ts_sec):
        return self.PTN_TEMPLATE[ptn_id] % ts_sec

    def __iter__(self):
        for sec in xrange(self.ts0, self.ts1, self.ts_inc):
            for ptn_id in self.ptn_ids:
                for host_id in self.host_ids:
                    pidx = ptn_id % self.n_daemons
                    hidx = host_id % self.n_daemons
                    if ptn_id and pidx != hidx:
                        continue
                    ts = abhttp.Timestamp(sec, 0)
                    host = host_name(host_id)
                    text = self.msg_text(ptn_id, sec)
                    msg = abhttp.LogMessage(ts, host, text, None, ptn_id)
                    yield msg
######## end of class MsgGenIter ########


class TestENV(object):
    def __init__(self, param={}):
        global gparam
        uparam = param
        param = Param(gparam)
        param.update(uparam)
        self.param = param

        # prep hosts file
        hfile = open(param.HOSTSFILE, "w")
        for hid in xrange(param.NODE_BEGIN, param.NODE_BEGIN + param.NODE_LEN):
            print >>hfile, "node%05d" % hid
        hfile.close()

        # start daemons
        self.balerd = []
        self.bhttpd = []
        self.feeder = []
        for i in range(0, param.N_DAEMONS):
            b = Balerd(i, param.TMP_DIR, param=param)
            b.cleanup()
            b.start()
            assert b.proc.poll() == None
            self.balerd.append(b)

        time.sleep(1.0) # wait a bit to make sure that stores has been created
                        # and balerd is listening on syslog port.

        for i in range(0, param.N_DAEMONS):
            log_port = param.SYSLOG_PORT_START + i
            f = subprocess.Popen(["./syslog2baler.pl", "-p", "%d" % log_port],
                                 stdin = subprocess.PIPE)
            self.feeder.append(f)

            if param.BHTTPD_MAIN:
                h = Bhttpd(i, param.TMP_DIR, bak=False, param=param)
                h.start()
                assert h.proc.poll() == None
                self.bhttpd.append(h)
            if param.BHTTPD_BAK:
                h = Bhttpd(i, param.TMP_DIR, bak=True, param=param)
                h.start()
                assert h.proc.poll() == None
                self.bhttpd.append(h)

        # feed small amount of data to bhttpd
        t_a = time.time()
        logger.info("Start piping data to balerd's")
        itr = MsgGenIter(ts0=param.TS_BEGIN,
                         ts1=param.TS_BEGIN+param.TS_LEN,
                         param=param)
        re_ptn = re.compile("node(\d+)")
        msg_count = 0
        for msg in itr:
            m = re_ptn.match(msg.host)
            assert m != None
            host_id = int(m.groups()[0])
            i = host_id % param.N_DAEMONS
            f = self.feeder[i]
            f.stdin.write(str(msg)+"\n")
            msg_count = msg_count + 1

        # close all data feeder, but leave balerd and bhttpd running.
        for f in self.feeder:
            f.stdin.close()
        time.sleep(1.0)
        t_b = time.time()

        logger.info("data piping done!")
        logger.info("     msg_count: %d", msg_count)
        logger.info("     time: %f", t_b - t_a)

    def __del__(self):
        for x in self.balerd:
            x.term()
        for x in self.bhttpd:
            x.term()
######## end of class TestENV ########

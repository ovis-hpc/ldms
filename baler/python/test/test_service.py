#!/usr/bin/env python
import logging
import unittest
import abhttp
import re
import subprocess
import time
import os
from StringIO import StringIO
from datetime import datetime

logger = logging.getLogger(__name__)

class TestConfig(unittest.TestCase):
    def setUp(self):
        self.CFG_PATH = ".test_service.cfg"
        f = open(self.CFG_PATH, "w")
        f.write("""\
bhttpd:
    bhttpd0: ["localhost:18000", "localhost:18800"]
    bhttpd1: ["localhost:18001", "localhost:18801"]
    bhttpd2: ["localhost:18002", "localhost:18802"]
    bhttpd3: ["localhost:18003", "localhost:18803"]
""")
        f.close()

    def test_config_file(self):
        cfg = abhttp.Config.from_yaml_file(self.CFG_PATH)
        logger.info("testing bhttpd section iterator.")
        for (name, addrs) in cfg.bhttpd_iter():
            logger.info("name(%s), addrs: %s", name, addrs)
        logger.info("------------------------------")

    def test_wrong_format(self):
        _str = """\
bhttpd:
    bhttpd0: ["localhost", "localhost:18800"]
    bhttpd1: ["localhost:18001", "localhost:18801"]
    bhttpd2: ["localhost:18002", "localhost:18802"]
    bhttpd3: ["localhost:18003", "localhost:18803"]
"""
        with self.assertRaises(TypeError):
            cfg = abhttp.Config.from_yaml_stream(_str)

    def tearDown(self):
        pass

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

class Param(object):
    pass #

param = Param()
param.N_DAEMONS = 4
param.N_PATTERNS = 8
param.TS_BEGIN = 1451628000
param.TS_LEN = 3600*24 # a day
param.TS_INC = 1800 # 2 msg/hour
param.NODE_BEGIN = 0
param.NODE_LEN = 64
param.TMP_DIR="./tmp"
param.SYSLOG_PORT_START = 30000
param.MASTER_PORT_START = 31000
param.HTTP_PORT_START = 18000
param.HTTP_BAK_PORT_START = 19000
param.LEVEL = "INFO"

# prep hosts file
hfile = open("hosts", "w")
for hid in xrange(param.NODE_BEGIN, param.NODE_BEGIN + param.NODE_LEN):
    print >>hfile, "node%05d" % hid
hfile.close()

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

    def __init__(self, iD, tmp_dir):
        global param
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
        global param
        subprocess.call("""
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
        global param
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

class Bhttpd(object):
    """Baler HTTPD control for testing."""

    def __init__(self, iD, tmp_dir, bak=False):
        global param
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

PTN_TEMPLATE = []

def gen_ptn_template(ptn_id):
    return "This is pattern %s: %%d" % num2text(ptn_id)

for i in range(0, param.N_PATTERNS):
    template = gen_ptn_template(i)
    PTN_TEMPLATE.append(template)

def msg_text(ptn_id, ts_sec):
    return PTN_TEMPLATE[ptn_id] % ts_sec

def host_name(i):
    return "node%05d" % i

host_ptn = re.compile("node(\d+)")
def host_id(name):
    return int(host_ptn.match(name).group(1))

class MsgGenIter(object):
    def __init__(self, uhost_ids=None, uptn_ids=None, ts0=None, ts1=None):
        global param
        ts_inc = param.TS_INC
        ts_begin = param.TS_BEGIN
        ts_len = param.TS_LEN
        node_begin = param.NODE_BEGIN
        node_len = param.NODE_LEN
        n_daemons = param.N_DAEMONS
        n_patterns = param.N_PATTERNS

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
                    text = msg_text(ptn_id, sec)
                    msg = abhttp.LogMessage(ts, host, text, None, ptn_id)
                    yield msg

# -- MsgGenIter -- #


class TestService(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        global param
        # start daemons
        cls.balerd = []
        cls.bhttpd = []
        cls.feeder = []
        tmp_dir = "./tmp/TestService"
        for i in range(0, param.N_DAEMONS):
            b = Balerd(i, tmp_dir)
            b.cleanup()
            b.start()
            assert b.proc.poll() == None
            cls.balerd.append(b)

        time.sleep(1.0) # wait a bit to make sure that stores has been created
                        # and balerd is listening on syslog port.

        for i in range(0, param.N_DAEMONS):
            log_port = param.SYSLOG_PORT_START + i
            f = subprocess.Popen(["./syslog2baler.pl", "-p", "%d" % log_port],
                                 stdin = subprocess.PIPE)
            cls.feeder.append(f)

            h = Bhttpd(i, tmp_dir, bak=True)
            h.start()
            assert h.proc.poll() == None
            cls.bhttpd.append(h)

        # feed small amount of data to bhttpd
        logger.info("Start piping data to balerd's")
        itr = MsgGenIter(ts0=param.TS_BEGIN, ts1=param.TS_BEGIN+2*param.TS_INC)
        re_ptn = re.compile("node(\d+)")
        for msg in itr:
            m = re_ptn.match(msg.host)
            assert m != None
            host_id = int(m.groups()[0])
            i = host_id % param.N_DAEMONS
            f = cls.feeder[i]
            f.stdin.write(str(msg)+"\n")

        # close all data feeder, but leave balerd and bhttpd running.
        for f in cls.feeder:
            f.stdin.close()
        time.sleep(1.0)

        logger.info("data piping done.")

        #cls is a class object here
        global num
        cfg_str = """\
bhttpd:
    srv0: ["localhost:18000", "localhost:19000"]
    srv1: ["localhost:18001", "localhost:19001"]
    srv2: ["localhost:18002", "localhost:19002"]
    srv3: ["localhost:18003", "localhost:19003"]
store: ./svc.store
        """

        cls.svc = abhttp.Service(cfg_stream=StringIO(cfg_str))
        cls.numeric_assign_host()
        cls.numeric_assign_ptn()

    @classmethod
    def numeric_assign_host(cls):
        hosts = [ x for x in cls.svc.uhost_iter() ]
        for (_id, _text) in hosts:
            m = re.match("node([0-9]+)", _text)
            hid = int(m.group(1))
            cls.svc.uhost_assign(hid, _text)

    @classmethod
    def numeric_assign_ptn(cls):
        ptns = [ x for x in cls.svc.uptn_iter() ]
        ptn = re.compile("^.*pattern " +
                "(((Zero|One|Two|Three|Four|Five|Six|Seven|Eight|Nine) *)+):")
        for p in ptns:
            m = re.match(ptn, p.text)
            t = m.group(1)
            n = 0
            for x in t.split(' '):
                n *= 10
                n += num[x]
            logger.debug("assigning id: %d, ptn: '%s'" % (n, p.text))
            cls.svc.uptn_assign(n, p.text)
            if re.match("This is pattern Zero: .*", p.text):
                cls.ptn_zero = n
        assert(cls.ptn_zero != None)

    def test_uptn_autoassign(self):
        svc = abhttp.Service(cfg_path="config.yaml")
        u0 = {p.text: p for p in svc.uptn_iter()}
        svc.uptn_autoassign()
        u1 = {p.text: p for p in svc.uptn_iter()}
        ids = set( p.ptn_id for p in svc.uptn_iter() )
        for p in svc.uptn_iter():
            self.assertTrue(p.ptn_id != None)
            ids.remove(p.ptn_id)
            del u0[p.text]
        self.assertTrue(len(u0) == 0)
        self.assertTrue(len(ids) == 0)

    def test_uhost(self):
        svc = self.svc
        s = set()
        count = 0
        for h in svc.uhost_iter():
            count += 1
            s.add(h.text)
            logger.debug("uhost - %s: %s", h.host_id, h.text)
        self.assertEqual(count, len(s))

    def test_uptn_update(self):
        global param
        svc = self.svc
        max_p = None
        for p in svc.uptn_iter():
            if max_p == None or max_p.ptn_id < p.ptn_id:
                max_p = p
        next_id = max_p.ptn_id + 1
        ptn_temp = gen_ptn_template(next_id)
        msg_text = ptn_temp % 0
        ts = abhttp.Timestamp(param.TS_BEGIN + param.TS_LEN, 0)
        msg = abhttp.LogMessage(ts, host_name(0), msg = msg_text)
        b = self.balerd[0]
        proc = subprocess.Popen(["./syslog2baler.pl", "-p", str(b.syslog_port)],
                                stdin=subprocess.PIPE)
        proc.stdin.write(str(msg) + "\n")
        proc.stdin.close()
        time.sleep(0.5) # make sure that balerd got the message
        svc.uptn_update()
        xptn = None
        for p in svc.uptn_iter():
            if p.ptn_id == None:
                self.assertTrue(xptn == None)
                xptn = p
        self.assertTrue(xptn != None)
        # xptn is the unassigned pattern.
        self.numeric_assign_ptn()
        ptn = svc.uptn_by_id(next_id)
        self.assertEqual(xptn.text, ptn.text)

    def test_uhost_update(self):
        global param
        svc = self.svc
        new_host = host_name(65536)
        ptn_temp = gen_ptn_template(0)
        msg_text = ptn_temp % 0
        ts = abhttp.Timestamp(param.TS_BEGIN + param.TS_LEN, 0)
        msg = abhttp.LogMessage(ts=ts, host=new_host, msg=msg_text)
        b = self.balerd[0]
        proc = subprocess.Popen(["./syslog2baler.pl", "-p", str(b.syslog_port)],
                                stdin=subprocess.PIPE)
        proc.stdin.write(str(msg) + "\n")
        proc.stdin.close()
        time.sleep(0.5) # make sure that balerd got the message
        svc.uhost_update()
        xhost = None
        for h in svc.uhost_iter():
            if h.host_id != None:
                continue
            xhost = h.text
        self.assertEqual(new_host, xhost)

    @classmethod
    def tearDownClass(cls):
        for x in cls.balerd:
            x.term()
        for x in cls.bhttpd:
            x.term()

# -- TestService -- #


class TestQueryIter(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # start daemons
        cls.balerd = []
        cls.bhttpd = []
        cls.feeder = []
        tmp_dir = "./tmp/TestUMsgQueryIter"
        for i in range(0, param.N_DAEMONS):
            b = Balerd(i, tmp_dir)
            b.cleanup()
            b.start()
            assert b.proc.poll() == None
            cls.balerd.append(b)

        time.sleep(1.0) # wait a bit to make sure that stores has been created
                        # and balerd is listening on syslog port.

        for i in range(0, param.N_DAEMONS):
            log_port = param.SYSLOG_PORT_START + i
            f = subprocess.Popen(["./syslog2baler.pl", "-p", "%d" % log_port],
                                 stdin = subprocess.PIPE)
            cls.feeder.append(f)

            h = Bhttpd(i, tmp_dir, bak=True)
            h.start()
            assert h.proc.poll() == None
            cls.bhttpd.append(h)

        # feed small amount of data to bhttpd
        logger.info("starting piping messages to balerd's")
        t_a  = time.time()
        itr = MsgGenIter()
        re_ptn = re.compile("node(\d+)")
        msg_count = 0
        for msg in itr:
            m = re_ptn.match(msg.host)
            assert m != None
            host_id = int(m.groups()[0])
            i = host_id % param.N_DAEMONS
            f = cls.feeder[i]
            f.stdin.write(str(msg)+"\n")
            msg_count += 1

        # close all data feeder, but leave balerd and bhttpd running.
        for f in cls.feeder:
            f.stdin.close()

        time.sleep(2.0) # wait a bit to make sure that balerd processed all the
                        # data.
        t_b = time.time()
        logger.info("data piping done!")
        logger.info("     msg_count: %d", msg_count)
        logger.info("     time: %f", t_b - t_a)

        #cls is a class object here
        cfg_str = """\
bhttpd:
    srv0: ["localhost:18000", "localhost:19000"]
    srv1: ["localhost:18001", "localhost:19001"]
    srv2: ["localhost:18002", "localhost:19002"]
    srv3: ["localhost:18003", "localhost:19003"]
store: ./svc.store
        """

        cls.svc = abhttp.Service(cfg_stream=StringIO(cfg_str))
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
        hosts = [ x for x in cls.svc.uhost_iter() ]
        for (_id, _text) in hosts:
            m = re.match("node([0-9]+)", _text)
            hid = int(m.group(1))
            cls.svc.uhost_assign(hid, _text)
        ptns = [ x for x in cls.svc.uptn_iter() ]
        ptn = re.compile("^.*pattern " +
                "(((Zero|One|Two|Three|Four|Five|Six|Seven|Eight|Nine) *)+):")
        for p in ptns:
            m = re.match(ptn, p.text)
            t = m.group(1)
            n = 0
            for x in t.split(' '):
                n *= 10
                n += num[x]
            logger.debug("assigning id: %d, ptn: '%s'" % (n, p.text))
            cls.svc.uptn_assign(n, p.text)
            if re.match("This is pattern Zero: .*", p.text):
                cls.ptn_zero = n
        assert(cls.ptn_zero != None)
        for p in cls.svc.uptn_iter():
            logger.debug(p)
        pass

    def _check_coll(self, coll, coll2, hid_set, pid_set):
        if not coll:
            return
        for hid in hid_set:
            for pid in pid_set:
                i0 = hid % self.n_daemons
                i1 = pid % self.n_daemons
                if pid and i0 != i1:
                    continue
                k = (hid, pid)
                msg = coll.pop(k)
                msg2 = coll2.pop(k)
        self.assertEqual(0, len(coll))
        self.assertEqual(0, len(coll2))
        pass

    def get_last_ts(self, ts):
        global param
        last_ts = param.TS_BEGIN + param.TS_LEN - 1
        if not ts or ts>last_ts:
            ts = last_ts
        inc = param.TS_INC
        return (ts/inc)*inc

    def run_query_correct(self, uhost_ids=None, uptn_ids=None,
                            ts0=None, ts1=None):
        # Test correctness of the query.
        itr = abhttp.UMsgQueryIter(self.svc, host_ids=uhost_ids,
                                    ptn_ids=uptn_ids, ts0=ts0, ts1=ts1)

        global param

        ts_inc      =  self.ts_inc      =  param.TS_INC
        ts_begin    =  self.ts_begin    =  param.TS_BEGIN
        ts_len      =  self.ts_len      =  param.TS_LEN
        node_begin  =  self.node_begin  =  param.NODE_BEGIN
        node_len    =  self.node_len    =  param.NODE_LEN
        n_daemons   =  self.n_daemons   =  param.N_DAEMONS
        n_patterns  =  self.n_patterns  =  param.N_PATTERNS

        if not uhost_ids:
            uhost_ids = "%d-%d" % (node_begin, node_begin + node_len -1)
        if not uptn_ids:
            uptn_ids = "%d-%d" % (0, n_patterns-1)
        if ts0 == None:
            ts0 = ts_begin
        if ts1 == None:
            ts1 = ts_begin + ts_len

        # round-up for ts0
        ts0 = int((ts0 + ts_inc - 1)/ts_inc)*ts_inc
        # truncate for ts1
        ts1 = self.get_last_ts(ts1)
        citr = MsgGenIter(uhost_ids, uptn_ids, ts0, ts1+1)

        hid_set = abhttp.IDSet()
        hid_set.add_smart(uhost_ids)
        pid_set = abhttp.IDSet()
        pid_set.add_smart(uptn_ids)

        # collect messages for each timestamp
        coll = {}
        coll2 = {}
        prev_ts = ts0

        for msg2 in citr:
            msg = itr.next()
            self.assertNotEqual(msg, None)
            if msg.ts.sec != prev_ts:
                self._check_coll(coll, coll2, hid_set, pid_set)
            prev_ts = msg.ts.sec
            hid = host_id(msg.host)
            pid = msg.ptn_id
            coll[(hid, pid)] = msg
            hid2 = host_id(msg2.host)
            pid2 = msg2.ptn_id
            coll2[(hid2, pid2)] = msg2

        self.assertEqual(prev_ts, ts1)

    def test_query_correct_cond0(self):
        self.run_query_correct(uptn_ids=[0])
        pass

    def test_query_correct_cond1(self):
        self.run_query_correct(uptn_ids=[1,4])
        pass

    def test_query_correct_cond2(self):
        self.run_query_correct(uptn_ids=[1,2])
        pass

    def test_query_correct_cond3(self):
        self.run_query_correct(uhost_ids=[0, 1, 2])
        pass

    def test_query_correct_cond4(self):
        global param
        ts0 = param.TS_BEGIN + param.TS_INC
        ts1 = ts0 + param.TS_LEN - 10
        self.run_query_correct(ts0=ts0, ts1=ts1)
        pass

    def test_query_correct_cond5(self):
        global param
        ts0 = param.TS_BEGIN + param.TS_INC
        ts1 = ts0 + param.TS_LEN - 10
        self.run_query_correct(ts0=ts0, ts1=ts1, uptn_ids="0-3", uhost_ids="0-2")

    def run_dir_cond(self, fwd_first=True, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        itr = abhttp.UMsgQueryIter(self.svc, host_ids=host_ids,
                                    ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        if fwd_first:
            _next = itr.next
            _prev = itr.prev
            _a = "-- FWD --"
            _b = "-- BWD --"
        else:
            _next = itr.prev
            _prev = itr.next
            _a = "-- BWD --"
            _b = "-- FWD --"
        stack = []
        i = 0
        logger.debug("")
        logger.debug(_a)
        x = _next()
        while x:
            i += 1
            logger.debug("%s", x)
            stack.append(x)
            x = _next()
            if n and i >= n:
                break;

        logger.debug("------")

        logger.debug("")
        logger.debug(_b)
        while i:
            x = _prev()
            logger.debug("%s", x)
            if not x:
                break;
            y = stack.pop()
            self.assertEqual(str(x), str(y))
            i -= 1
        self.assertEqual(0, len(stack))
        self.assertEqual(0, i)
        del itr
        logger.debug("------")
        pass

    def run_fwd_bwd_cond(self, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        self.run_dir_cond(fwd_first=True, n=n, host_ids=host_ids,
                            ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)

    def run_bwd_fwd_cond(self, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        self.run_dir_cond(fwd_first=False, n=n, host_ids=host_ids,
                            ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)

    def test_fwd_bwd_no_cond(self):
        self.run_fwd_bwd_cond()
        pass

    def test_fwd_bwd_cond0(self):
        self.run_fwd_bwd_cond(n=5)
        pass

    def test_bwd_fwd_no_cond(self):
        self.run_bwd_fwd_cond()
        pass

    def test_bwd_fwd_cond0(self):
        self.run_bwd_fwd_cond(n=5)
        pass

    def test_ptn_ids(self):
        self.run_fwd_bwd_cond(ptn_ids=str(self.ptn_zero))
        self.run_bwd_fwd_cond(ptn_ids=str(self.ptn_zero))
        pass

    def run_test_fwd(self, host_ids=None, ptn_ids=None, ts0=None, ts1=None):
        logger.debug("ptn_ids: %s", ptn_ids)
        for x in self.svc.uptn_iter():
            logger.debug("%s", x)
        itr = abhttp.UMsgQueryIter(self.svc, host_ids=host_ids,
                                    ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        msg = itr.next()
        count = 0
        while msg:
            count += 1
            msg = itr.next()
        logger.debug("count: %s", count)
        pass

    def x_test_fwd_case0(self):
        self.run_test_fwd(ptn_ids=self.ptn_zero)
        pass

    def run_test_bwd(self):
        itr = abhttp.UMsgQueryIter(self.svc)
        msg = itr.prev()
        count = 0
        while msg:
            count += 1
            msg = itr.prev()
        logger.debug("count: %s", count)
        pass

    def run_dir_pos_cond(self, fwd_first=True, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        itr = abhttp.UMsgQueryIter(self.svc, host_ids=host_ids,
                                    ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        itr2 = abhttp.UMsgQueryIter(self.svc, host_ids=host_ids,
                                    ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        if fwd_first:
            _next = itr.next
            _posdir = abhttp.BWD
            _prev = itr2.prev
            _a = "-- FWD --"
            _b = "-- BWD --"
        else:
            _next = itr.prev
            _posdir = abhttp.FWD
            _prev = itr2.next
            _a = "-- BWD --"
            _b = "-- FWD --"
        stack = []
        i = 0
        logger.debug("")
        logger.debug(_a)
        x = _next()
        pos = None
        while x:
            i += 1
            logger.debug("%s", x)
            stack.append(x)
            pos = itr.get_pos()
            x = _next()
            if n and i >= n:
                break;

        logger.debug("------")

        logger.debug("")
        logger.debug(_b)
        logger.debug("pos: %s", pos)

        itr2.set_pos(pos)
        pos2 = itr2.get_pos()
        logger.debug("pos2: %s", pos2)

        self.assertEqual(pos, pos2)

        x = itr2.get_curr_msg()
        y = stack.pop()
        self.assertEqual(str(x), str(y))
        i -= 1

        while i:
            x = _prev()
            logger.debug("%s", x)
            if not x:
                break;
            y = stack.pop()
            self.assertEqual(str(x), str(y))
            i -= 1
        self.assertEqual(0, len(stack))
        self.assertEqual(0, i)
        del itr
        logger.debug("------")
        pass

    def test_dir_pos_no_cond(self):
        self.run_dir_pos_cond(n = 5)

    def test_dir_pos_cond0(self):
        self.run_dir_pos_cond(n =5, ptn_ids=str(self.ptn_zero))


    def run_img_query_correct(self, uhost_ids=None, uptn_ids=None,
                              ts0=None, ts1=None):
        # Test correctness of the image query.
        uitr = abhttp.UImgQueryIter(service=self.svc, img_store="3600-1",
                                    host_ids=uhost_ids, ptn_ids=uptn_ids,
                                    ts0=ts0, ts1=ts1
                                )
        citr = MsgGenIter(uhost_ids, uptn_ids, ts0, ts1)
        pxl_buff = {}
        for msg in citr:
            logger.debug("msg: %s", msg)
            key_sec = int(msg.ts.sec/3600) * 3600
            pxl = abhttp.Pixel(ptn_id=msg.ptn_id,
                               sec=key_sec,
                               comp_id=host_id(msg.host),
                               count=0)
            try:
                pxl = pxl_buff[pxl.key]
            except KeyError:
                pxl_buff[pxl.key] = pxl
            pxl.count += 1
        pxls = [p for p in pxl_buff.values()]
        pxls.sort()
        for p in pxls:
            logger.debug("p: %s", p)
        logger.debug("p count: %d", len(pxls))
        for pxl in uitr:
            p = pxl_buff.pop(pxl.key)
            self.assertEqual(p, pxl)
        if len(pxl_buff):
            pxls = [p for p in pxl_buff.values()]
            pxls.sort()
            for p in pxls:
                logger.debug("junk p: %s", p)
        self.assertTrue(len(pxl_buff) == 0)

    def test_img_query_cond0(self):
        self.run_img_query_correct()

    def test_img_query_cond1(self):
        self.run_img_query_correct(uptn_ids=0)

    def test_img_query_cond2(self):
        ts0 = int(param.TS_BEGIN/3600)*3600
        ts1 = ts0 + 2*3600 - 1
        logger.debug("---")
        logger.debug("ts0: %d", ts0)
        logger.debug("ts1: %d", ts1)
        for sec in xrange(ts0, ts1, param.TS_INC):
            logger.debug("sec: %d", sec)
        logger.debug("---")
        self.run_img_query_correct(ts0=ts0,
                                   ts1=ts1)

    def test_img_query_cond3(self):
        ts0 = int(param.TS_BEGIN/3600)*3600
        ts1 = ts0 + 2*3600 - 1
        self.run_img_query_correct(uptn_ids="0", uhost_ids="0",
                                   ts0=ts0,
                                   ts1=ts1)


    @classmethod
    def tearDownClass(cls):
        #self is a class object here
        del cls.svc
        for x in cls.balerd:
            x.term()
        for x in cls.bhttpd:
            x.term()

# -- TestUMsgQueryIter -- #


if __name__ == "__main__":
    LOGFMT = '%(asctime)s %(name)s %(funcName)s %(levelname)s: %(message)s'
    logging.basicConfig(format=LOGFMT)
    logger.setLevel(logging.INFO)
    unittest.main()

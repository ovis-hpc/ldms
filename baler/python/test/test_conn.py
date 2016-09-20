#!/usr/bin/env python
import logging
import unittest
import abhttp
import util

logger = logging.getLogger(__name__)

testENV = util.TestENV(util.Param(
                    TMP_DIR='./tmp/test_conn',
                    N_DAEMONS = 1,
                ))

class TestConnBase(object):
    def setUp(self):
        """ subclass must provide `self.conn` """
        raise Exception("Override me!")

    def test_get_fetch(self):
        self.conn.get_ptn()
        ptns = self.conn.fetch_ptn()
        logger.info("number of patterns: %d", len(ptns))

    def test_img2(self):
        self.conn.get_img2(img_store="3600-1",
                            ts_begin=1435294800,
                            host_begin=0,
                            spp=3600,
                            npp=1,
                            width=40,
                            height=40,
                            ptn_ids="128")
        img = self.conn.fetch_img2()
        logger.info(img)
        self.assertEqual(len(img), 40*40, "wrong image length")

    def test_img(self):
        self.conn.get_img(img_store="3600-1")
        fetch_count = 1
        pxl_count = 0
        pxls = self.conn.fetch_img(100)
        while pxls:
            pxl_count += len(pxls)
            for p in pxls:
                logger.info("p: %s", str(p))
            pxls = self.conn.fetch_img(100)
            fetch_count+=1
        logger.info("fetch_count: %d", fetch_count)
        logger.info("pxl_count: %d", pxl_count)

    def test_ptn(self):
        self.conn.get_ptn()
        ptns = self.conn.fetch_ptn()
        self.assertGreater(len(ptns), 1, "No patterns")
        logger.info("len(ptns): %d", len(ptns))
        for p in ptns.items():
            logger.info(str(p))

    def test_msg(self):
        conn = self.conn
        conn.get_msg(n=4)
        (sid, msgs) = conn.fetch_msg()
        self.assertTrue(sid, "No sid")
        self.assertEqual(4, len(msgs))
        logger.info("len(msgs): %d", len(msgs))
        for m in msgs:
            logger.info(str(m))
        conn.get_msg(session_id=sid, n=4)
        (sid2, msgs) = conn.fetch_msg()
        self.assertEqual(4, len(msgs))
        self.assertEqual(sid, sid2, "sid != sid2")
        logger.info("len(msgs): %d", len(msgs))
        for m in msgs:
            logger.info(str(m))

    def test_host(self):
        conn = self.conn
        conn.get_host()
        m = conn.fetch_host()
        count = 0
        for (_id, _str) in m.items():
            count += 1
            logger.info("host[%s]: %s", _id, _str)
######## end of class TestConnBase ########


class TestBHTTPDConn(unittest.TestCase, TestConnBase):
    def setUp(self):
        self.conn = abhttp.BHTTPDConn("localhost:18000")

    def tearDown(self):
        del self.conn


class TestBStore2Conn(unittest.TestCase, TestConnBase):
    def setUp(self):
        self.conn = abhttp.BStore2Conn(testENV.param.TMP_DIR + "/store.0")

    def tearDown(self):
        del self.conn


class TestConnMsgQueryIterBase(object):
    @classmethod
    def setUpClass(cls):
        """ subclass must provide `cls.conn` """
        raise Exception("Override me!")

    def run_bwd_fwd_cond(self, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        itr = abhttp.BHTTPDConnMsgQueryIter(self.conn,
                    host_ids=host_ids, ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        itr.BUFSLOTS = 4
        stack = []
        i = 0
        logger.info("")
        logger.info("-- BWD --")
        x = itr.prev()
        while x:
            i += 1
            stack.append(x)
            logger.info("%s", str(x))
            x = itr.prev()
            if n and i >= n:
                break;

        logger.info("------")

        logger.info("")
        logger.info("-- FWD --")
        while i:
            x = itr.next()
            if not x:
                break;
            logger.info("%s", str(x))
            y = stack.pop()
            self.assertEqual(str(x), str(y))
            i -= 1
        self.assertEqual(0, len(stack))
        self.assertEqual(0, i)
        logger.info("------")
        del itr

    def run_fwd_bwd_cond(self, n=None, host_ids=None,
                         ptn_ids=None, ts0=None, ts1=None):
        itr = abhttp.BHTTPDConnMsgQueryIter(self.conn,
                    host_ids=host_ids, ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        itr.BUFSLOTS = 4
        stack = []
        i = 0
        logger.info("")
        logger.info("-- FWD --")
        x = itr.next()
        while x:
            i += 1
            stack.append(x)
            logger.info("%s", str(x))
            x = itr.next()
            if n and i >= n:
                break;

        logger.info("------")

        logger.info("")
        logger.info("-- BWD --")
        while i:
            x = itr.prev()
            if not x:
                break;
            logger.info("%s", str(x))
            y = stack.pop()
            self.assertEqual(str(x), str(y))
            i -= 1
        self.assertEqual(0, len(stack))
        self.assertEqual(0, i)
        logger.info("------")
        del itr

    def test_fwd_bwd_no_cond(self):
        self.run_fwd_bwd_cond(n=11)
        self.run_fwd_bwd_cond()
        pass

    def test_bwd_fwd_no_cond(self):
        self.run_bwd_fwd_cond(n=11)
        self.run_bwd_fwd_cond()
        pass

    def test_fwd_bwd_cond(self):
        self.run_fwd_bwd_cond(ptn_ids=128, host_ids="0-5",
                              ts0="2015-06-26 11:00:00.000000",
                              ts1="2015-06-26 15:00:00.000000",
                              n=5
                              )
        self.run_fwd_bwd_cond(ptn_ids=128, host_ids="0-5",
                              ts0="2015-06-26 11:00:00.000000",
                              ts1="2015-06-26 15:00:00.000000"
                              )
        pass

    def test_bwd_fwd_cond(self):
        self.run_bwd_fwd_cond(ptn_ids=128, host_ids="0-5",
                              ts0="2015-06-26 11:00:00.000000",
                              ts1="2015-06-26 15:00:00.000000",
                              n=5
                              )
        self.run_bwd_fwd_cond(ptn_ids=128, host_ids="0-5",
                              ts0="2015-06-26 11:00:00.000000",
                              ts1="2015-06-26 15:00:00.000000"
                              )
        pass

    def run_test_pos(self, n=None, ptn_ids=None, host_ids=None, ts0=None, ts1=None):
        itr = abhttp.BHTTPDConnMsgQueryIter(self.conn,
                    host_ids=host_ids, ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        stack = []
        i = 0
        logger.info("")
        logger.info("-- FWD --")
        pos = None
        x = itr.next()
        while x:
            i += 1
            stack.append(x)
            pos = itr.get_pos()
            logger.info("%s", str(x))
            x = itr.next()
            if n and i >= n:
                break;

        logger.info("------")

        logger.info("")
        logger.info("-- BWD --")
        itr2 = abhttp.BHTTPDConnMsgQueryIter(self.conn,
                    host_ids=host_ids, ptn_ids=ptn_ids, ts0=ts0, ts1=ts1)
        x = itr2.set_pos(pos, direction = abhttp.BWD)
        logger.info("%s", str(x))
        y = stack.pop()
        self.assertEqual(str(x), str(y))
        i -= 1
        while i:
            x = itr2.prev()
            if not x:
                break;
            logger.info("%s", str(x))
            y = stack.pop()
            self.assertEqual(str(x), str(y))
            i -= 1
        self.assertEqual(0, len(stack))
        self.assertEqual(0, i)
        logger.info("------")
        del itr
        pass

    def test_pos_no_cond(self):
        self.run_test_pos(n=3)

    def test_pos_cond(self):
        param = testENV.param
        ts0 = param.TS_BEGIN + param.TS_INC
        ts1 = ts0 + 2*param.TS_INC
        self.run_test_pos(n = 5, ptn_ids=128, host_ids="0-5",
                              ts0=ts0,
                              ts1=ts1
                          )

    def test_eof(self):
        logger.warn("self.conn: %s", self.conn)
        itr = abhttp.BHTTPDConnMsgQueryIter(self.conn)
        a = itr.next()
        logger.info("a.pos: %s", a.pos)
        b = itr.next()
        b = itr.prev()
        c = itr.prev()
        self.assertIsNone(c)
        self.assertEqual(str(a), str(b))
        self.assertEqual(a, b)
        del itr
        pass
######## end of class TestConnMsgQueryIterBase ########


class TestBHTTPDConnMsgQueryIter(unittest.TestCase, TestConnMsgQueryIterBase):
    @classmethod
    def setUpClass(cls):
        cls.conn = abhttp.BHTTPDConn("localhost:18000")
        pass

    @classmethod
    def tearDownClass(cls):
        del cls.conn


class TestBStore2MsgQueryIter(unittest.TestCase, TestConnMsgQueryIterBase):
    @classmethod
    def setUpClass(cls):
        cls.conn = abhttp.BStore2Conn(testENV.param.TMP_DIR + "/store.0")
        logger.warn("self.conn: %s", cls.conn)

    @classmethod
    def tearDownClass(cls):
        del cls.conn


if __name__ == "__main__":
    LOGFMT = '%(asctime)s %(name)s %(levelname)s: %(message)s'
    logging.basicConfig(format=LOGFMT)
    # logger.setLevel(logging.INFO)
    unittest.main()
    del testEnv

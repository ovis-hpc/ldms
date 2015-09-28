#!/usr/bin/env python
import os
import sys
from baler import bquery

BSTORE = os.environ['BSTORE']
BTEST_TS_BEGIN = int(os.environ['BTEST_TS_BEGIN'])
BTEST_TS_LEN = int(os.environ['BTEST_TS_LEN'])
BTEST_TS_INC = int(os.environ['BTEST_TS_INC'])
BTEST_NODE_BEGIN = int(os.environ['BTEST_NODE_BEGIN'])
BTEST_NODE_LEN = int(os.environ['BTEST_NODE_LEN'])

bstore = bquery.bq_open_store(BSTORE)
assert bstore

def msg_iter(node_range, ptn_range, ts0, ts1, exp_msg_per_ts, exp_msg):
    msgs = []
    if ts0:
        ts0 = str(ts0)
    else:
        ts0 = None
    if ts1:
        ts1 = str(ts1)
    else:
        ts1 = None
    print "Checking MSG iter | node_range: '%s', ptn_range: '%s', " \
            "ts0: '%s', ts1: '%s'" % (node_range, ptn_range, ts0, ts1)
    bmq = bquery.bmsgquery_create(bstore, node_range, ptn_range,
                                        ts0, ts1, 0, ' ', None)
    assert bmq
    bq = bquery.bmq_to_bq(bmq)

    n = 0
    rc = bquery.bq_first_entry(bq)
    prev_sec = 0
    while rc == 0 and n < 10:
        sec = bquery.bq_entry_get_sec(bq)
        usec = bquery.bq_entry_get_sec(bq)
        comp_id = bquery.bq_entry_get_comp_id(bq)
        msg_str = bquery.bq_entry_get_msg_str(bq)
        s = "%d %d %d %s" % (sec, usec, comp_id, msg_str)
        msgs.append(s)
        n = n + 1
        # print "%d.%d (%d) %s" % (sec, usec, comp_id, msg_str)
        rc = bquery.bq_next_entry(bq)

    while n:
        rc = bquery.bq_prev_entry(bq)
        assert(rc == 0)
        sec = bquery.bq_entry_get_sec(bq)
        usec = bquery.bq_entry_get_sec(bq)
        comp_id = bquery.bq_entry_get_comp_id(bq)
        msg_str = bquery.bq_entry_get_msg_str(bq)
        s = "%d %d %d %s" % (sec, usec, comp_id, msg_str)
        _s = msgs.pop()
        if (_s != s):
            print "strings not matched"
            print "    _s:", _s
            print "     s:", s
        assert(_s == s)

        n = n - 1

def img_iter(node_range, ptn_range, ts0, ts1, exp_count_per_pixel, exp_pixel):
    if ts0:
        ts0 = str(ts0)
    else:
        ts0 = None
    if ts1:
        ts1 = str(ts1)
    else:
        ts1 = None
    print "Checking IMG iter | node_range: '%s', ptn_range: '%s', " \
            "ts0: '%s', ts1: '%s'" % (node_range, ptn_range, ts0, ts1)
    biq = bquery.bimgquery_create(bstore, node_range, ptn_range, ts0, ts1, "3600-1", None)
    assert biq
    n = 0
    rc = bquery.biq_first_entry(biq)
    pixels = []
    while rc == 0 and n < 10:
        p = bquery.biq_entry_get_pixel(biq)
        pixels.append(p)
        rc = bquery.biq_next_entry(biq)

    while n:
        rc = bquery.biq_prev_entry(biq)
        assert(rc == 0)
        p = bquery.biq_entry_get_pixel(biq)
        _p = pixels.pop()
        assert(p == _p)
        n = n - 1


# Some variables ...
inc = BTEST_TS_INC

if inc < 3600 :
    inc = 3600

# host only
ts0 = None
ts1 = None
node_range = "1-10"
ptn_range = None
mps = BTEST_NODE_LEN
ts_n = int(inc / BTEST_TS_INC)
img_iter(node_range, ptn_range, ts0, ts1, int(inc/BTEST_TS_INC), BTEST_NODE_LEN)
msg_iter(node_range, ptn_range, ts0, ts1, BTEST_NODE_LEN, mps * ts_n)

# timestamp only
ts0 = (BTEST_TS_BEGIN) + (BTEST_TS_LEN) - 2 * inc - 1
ts1 = (BTEST_TS_BEGIN) + (BTEST_TS_LEN) - inc - 1
node_range = None
ptn_range = None
mps = BTEST_NODE_LEN
ts_n = int(inc / BTEST_TS_INC)
img_iter(node_range, ptn_range, ts0, ts1, int(inc/BTEST_TS_INC), BTEST_NODE_LEN)
msg_iter(node_range, ptn_range, ts0, ts1, BTEST_NODE_LEN, mps * ts_n)

# ptn_id only
ts0 = None
ts1 = None
node_range = None
ptn_range = "129,130"
mps = BTEST_NODE_LEN
ts_n = int(BTEST_TS_LEN / BTEST_TS_INC)
hr_n = int(BTEST_TS_LEN / 3600)
img_iter(node_range, ptn_range, ts0, ts1, int(inc/BTEST_TS_INC), hr_n * BTEST_NODE_LEN)
msg_iter(node_range, ptn_range, ts0, ts1, BTEST_NODE_LEN, mps * ts_n)

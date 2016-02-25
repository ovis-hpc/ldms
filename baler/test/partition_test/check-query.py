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
BTEST_N_PATTERNS = int(os.environ['BTEST_N_PATTERNS'])

bstore = bquery.bq_open_store(BSTORE)
assert bstore

def msg_query(node_range, ptn_range, ts0, ts1, exp_msg_per_ts, exp_msg):
    if ts0:
        ts0 = str(ts0)
    else:
        ts0 = None
    if ts1:
        ts1 = str(ts1)
    else:
        ts1 = None
    print "Checking MSG query | node_range: '%s', ptn_range: '%s', " \
            "ts0: '%s', ts1: '%s'" % (node_range, ptn_range, ts0, ts1)
    bmq = bquery.bmsgquery_create(bstore, node_range, ptn_range,
                                        ts0, ts1, 0, ' ', None)
    assert bmq
    bq = bquery.bmq_to_bq(bmq)

    bq_count = 0
    n = 0
    rc = bquery.bq_first_entry(bq)
    prev_sec = 0
    while rc == 0:
        bq_count = bq_count + 1

        sec = bquery.bq_entry_get_sec(bq)
        usec = bquery.bq_entry_get_sec(bq)
        comp_id = bquery.bq_entry_get_comp_id(bq)
        msg_str = bquery.bq_entry_get_msg_str(bq)
        if sec != prev_sec:
            assert (prev_sec == 0 or (sec - prev_sec) == BTEST_TS_INC)
            if n and n != exp_msg_per_ts:
                print "query ts: %d has only %d messages, expecting %d messages" % \
                    (prev_sec, n, BTEST_NODE_LEN)
                exit(-1)
            n = 0
        n = n + 1
        prev_sec = sec
        # print "%d.%d (%d) %s" % (sec, usec, comp_id, msg_str)
        rc = bquery.bq_next_entry(bq)
    if exp_msg != bq_count:
        print "Expecting %d, bq_count: %d" % (exp_msg, bq_count)
        exit(-1)

def img_query(node_range, ptn_range, ts0, ts1, exp_count_per_pixel, exp_pixel):
    if ts0:
        ts0 = str(ts0)
    else:
        ts0 = None
    if ts1:
        ts1 = str(ts1)
    else:
        ts1 = None
    print "Checking IMG query | node_range: '%s', ptn_range: '%s', " \
            "ts0: '%s', ts1: '%s'" % (node_range, ptn_range, ts0, ts1)
    biq = bquery.bimgquery_create(bstore, node_range, ptn_range, ts0, ts1, "3600-1", None)
    assert biq
    biq_count = 0
    rc = bquery.biq_first_entry(biq)
    while rc == 0:
        biq_count = biq_count + 1
        p = bquery.biq_entry_get_pixel(biq)
        if p.count != exp_count_per_pixel:
            print "<ptn_id: %d, sec: %d, comp_id: %d, count: %d>: " \
                  "count in correct (expecting %d)" % \
                  (p.ptn_id, p.sec, p.comp_id, p.count, exp_count_per_pixel)
            exit(-1)
        rc = bquery.biq_next_entry(biq)

    if (exp_pixel != biq_count):
        print "Expecting %d pixels, but got: %d pixels" % (exp_pixel, biq_count)
        exit(-1)

# Some variables ...
inc = BTEST_TS_INC

if inc < 3600 :
    inc = 3600

def expect_node(ptn_num):
    ptn_num = int(ptn_num)
    ptn_num -= 128
    n = 0;
    while (n < BTEST_NODE_LEN):
        node = BTEST_NODE_BEGIN + n
        n = n + 1
    return BTEST_NODE_LEN

# key existing case
ts0 = (BTEST_TS_BEGIN) + (BTEST_TS_LEN) - 2 * inc
ts1 = (BTEST_TS_BEGIN) + (BTEST_TS_LEN) - inc - 1
node_range = None
ptn_id = 128
ptn_range = str(ptn_id)
mps = expect_node(ptn_id)
ts_n = int(inc / BTEST_TS_INC)
img_query(node_range, ptn_range, ts0, ts1, int(inc/BTEST_TS_INC), mps)
msg_query(node_range, ptn_range, ts0, ts1, mps, mps * ts_n)

# key not existing case
ts0 = (BTEST_TS_BEGIN) + (BTEST_TS_LEN) - 2 * inc - 1
ts1 = (BTEST_TS_BEGIN) + (BTEST_TS_LEN) - inc - 1
node_range = None
ptn_id = 128
ptn_range = str(ptn_id)
mps = expect_node(ptn_id)
ts_n = int(inc / BTEST_TS_INC)
img_query(node_range, ptn_range, ts0, ts1, int(inc/BTEST_TS_INC), mps)
msg_query(node_range, ptn_range, ts0, ts1, mps, mps * ts_n)

# another key not existing case
ts0 = 20
ts1 = None
node_range = None
ptn_id = 128 + BTEST_N_PATTERNS - 1
ptn_range = str(ptn_id)
mps = expect_node(ptn_id)
ts_n = int(BTEST_TS_LEN / BTEST_TS_INC)
hr_n = int(BTEST_TS_LEN / 3600)
img_query(node_range, ptn_range, ts0, ts1, int(inc/BTEST_TS_INC), hr_n * mps)
msg_query(node_range, ptn_range, ts0, ts1, mps, mps * ts_n)

# Multiple patterns
ts0 = 20
ts1 = None
node_range = None
ptn_ids = [128, 128 + BTEST_N_PATTERNS - 1]
ptn_range = ','.join(str(p) for p in ptn_ids)
mps = sum(expect_node(x) for x in ptn_ids)
ts_n = int(BTEST_TS_LEN / BTEST_TS_INC)
hr_n = int(BTEST_TS_LEN / 3600)
img_query(node_range, ptn_range, ts0, ts1, int(inc/BTEST_TS_INC), hr_n * mps)
msg_query(node_range, ptn_range, ts0, ts1, mps, mps * ts_n)

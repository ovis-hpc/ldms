#!/usr/bin/python3
#
# This is a stand-alone test that runs on a single node. The test process will
# spawn the following processes:
# - `async_server`: an example implementaion of an LDMS provider with Python
#   using asynchronous programming pattern (callback). This process listens on
#   port 10000.
# - `sync_server`: an example implementation of an LDMS provider with Python
#   using synchronous programming pattern (blocking function call). This process
#   listens on port 10001.
# - `ldmsd`: an aggregator collecting from `async_server` and `sync_server`.
#   This process listens on 10002.
# - `ldms_ls`: to query results from `async_server`, `sync_server` and `ldmsd`
#   aggregator.
#
# The following is the layout of the test:
# [x] LDMS Python providers (`async_server` and `sync_server`)
#     [x] query using `ldms_ls -lv`
# [x] setup `ldmsd` aggregator collecting data from the Python provider
#     [x] verify data on the `ldmsd` using `ldms_ls -lv`
# [x] query `ldmsd` using Python (sync and async varients)
#     [x] connect
#     [x] dir
#     [x] lookup
#     [x] update
#     [x] get metric values
#     [x] get metatada
# [x] query LDMS Python provider using Python (sync and async varients)
#     [x] connect
#     [x] dir
#     [x] lookup
#     [x] update
#     [x] get metric values
#     [x] get metatada

import os
import io
import pdb
import sys
import time
import subprocess as sp
import threading as thread
from queue import Queue
from ovis_ldms import ldms
from set_test import parse_ldms_ls, EXPECTED_DATA, SET_NAME, SCHEMA_NAME, UNITS

ldms.init(16*1024*1024)

class Debug(object):
    pass
D = Debug()

os.setpgrp()

print("INFO: spawning async_server")
async_server = sp.Popen(["/usr/bin/python3", "-i", "async_server.py"], bufsize=4096,
                        stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE)
print("INFO: spawning sync_server")
sync_server = sp.Popen(["/usr/bin/python3", "-i", "sync_server.py"], bufsize=4096,
                       stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE)
print("INFO: spawning ldmsd aggregator")
agg = sp.Popen(["/usr/bin/python3", "-i", "agg.py"], bufsize=4096,
                       stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE)

def ldms_ls(port, l=False, v=False):
    cmd = "ldms_ls -x sock -p {port} {l} {v}".format(
                port=port,
                l='-l' if l else '',
                v='-v' if v else '',
            )
    r = sp.run(cmd, shell=True, bufsize=4096,
               stdin=None, stdout=sp.PIPE, stderr=sp.PIPE)
    D.ls_result = r
    return parse_ldms_ls(r.stdout.decode())

def check(text, cond):
    """Pretty print condition checking"""
    PASSED = "\033[1;32mPASSED\033[0m"
    FAILED = "\033[1;31mFAILED\033[0m"
    print(text, ":", PASSED if cond else FAILED)
    if not cond:
        raise RuntimeError(text)

time.sleep(5.0) # allow some time for LDMS processes to initialize

async_ls = ldms_ls(10000, l=True, v=True)
sync_ls = ldms_ls(10001, l=True, v=True)
agg_ls = ldms_ls(10002, l=True, v=True)

print("=== check ldms_ls results from async_server ===")
d = list(async_ls.values())
check("  check number of sets", len(d) == 1)
d0 = d[0]
check("  check set name", d0["name"] == "async/"+SET_NAME)
check("  check schema name", d0["meta"]["schema"] == SCHEMA_NAME)
check("  check set data", d0["data"] == EXPECTED_DATA)

print("=== check ldms_ls results from sync_server ===")
d = list(sync_ls.values())
check("  check number of sets", len(d) == 1)
d0 = d[0]
check("  check set name", d0["name"] == "sync/"+SET_NAME)
check("  check schema name", d0["meta"]["schema"] == SCHEMA_NAME)
check("  check set data", d0["data"] == EXPECTED_DATA)

print("=== check ldms_ls results from agg ===")
d = list(agg_ls.values())
check("  check number of sets", len(d) == 2)
check("  check set name", set(m["name"] for m in d) == \
                          set(["sync/"+SET_NAME, "async/"+SET_NAME]))
check("  check schema name", set(m["meta"]["schema"] for m in d) == \
                             set([SCHEMA_NAME]))
for m in d:
    check("  check data for {}".format(m["name"]), m["data"] == EXPECTED_DATA)

#######################################
#     Python async client to ldmsd    #
#######################################
D.dir_more = -1
D.lookup_more = -1
D.updated = 0
async_dlist = [] # list of dir result
async_slist = [] # list of LDMS set
async_cond = thread.Condition()

print("=== check asynchronous Python ldms client querying ldmsd ===")

def update_cb(lset, flags, arg):
    global async_cond
    check("  update_cb({}) arg check".format(lset.name), arg == 40)
    D.update_arg = arg
    D.update_flags = flags
    D.updated += 1
    async_cond.acquire()
    async_cond.notify()
    async_cond.release()

def lookup_cb(xprt, status, more, lset, arg):
    check("  lookup_cb({}) arg check".format(lset.name), arg == 30)
    global async_slist
    D.lookup_arg = arg
    D.lookup_more = more
    if lset:
        async_slist.append(lset)
        lset.update(update_cb, arg+10)

def dir_cb(xprt, status, dd, arg):
    check("  dir_cb({}) arg check".format(xprt), arg == 20)
    D.dir_arg = arg
    if dd.type != ldms.DIR_LIST:
        return # ignore other types
    D.dir_more = dd.more
    for sd in dd.set_data:
        global async_dlist
        async_dlist.append(sd.inst_name)
        xprt.lookup(sd.inst_name, cb=lookup_cb, cb_arg=arg+10)

recv_queue = Queue()

def xprt_cb(xprt, ev, arg):
    # D.t = type(ev.type)
    D.ev = ev
    D.ev_type = ev.type
    D.ev_data = ev.data
    D.xprt_arg = arg
    D.xprt = xprt
    if D.ev.type == ldms.EVENT_CONNECTED:
        check("  xprt_cb({}) CONNECTED arg check".format(xprt), arg == 10)
        D.connected = 1
        xprt.dir(cb = dir_cb, cb_arg = arg+10)
    elif D.ev.type == ldms.EVENT_DISCONNECTED:
        check("  xprt_cb({}) DISCONNECTED arg check".format(xprt), arg == 10)
        D.connected = 0
        async_cond.acquire()
        async_cond.notify()
        async_cond.release()
    elif D.ev.type == ldms.EVENT_RECV:
        global recv_queue
        recv_queue.put(ev.data)

x = ldms.Xprt()
x.connect(host="localhost", port=10002, cb=xprt_cb, cb_arg=10)
t0 = time.time()
DT = 3.0
while D.updated < 2 and (time.time() - t0 < DT):
    async_cond.acquire()
    async_cond.wait(timeout = 1.0)
check("  async update completed", D.updated == 2)
check("  dir result check", set(async_dlist) == \
                            set(["async/"+SET_NAME, "sync/"+SET_NAME]))
check("  schema name check", set(s.schema_name for s in async_slist) == \
                             set([ 'simple' ]))
for s in async_slist:
    check("  {} data check".format(s.name), s.as_dict() == EXPECTED_DATA)
    # Enable this when unit support is ported from v5
    #check("  {} units check".format(s.name),
    #                [ s.units(i) for i in range(0, len(s)) ] == UNITS)
    pname = s.name.split('/')[0]
    check("  {} producer name check".format(s.name), s.producer_name == pname)
for s in async_slist:
    s.delete()
del async_slist
x.close()
t0 = time.time()
DT = 3.0
while D.connected and (time.time() - t0 < DT):
    async_cond.acquire()
    async_cond.wait(timeout = 1.0)
check("  async close completed", D.connected == 0)

####################################################
#     Python async client to Python sync_server    #
####################################################
D.dir_more = -1
D.lookup_more = -1
D.updated = 0
async_dlist = [] # list of dir result
async_slist = [] # list of LDMS set

print("=== check asynchronous Python ldms client querying Python sync_server ===")

x = ldms.Xprt()
x.connect(host="localhost", port=10001, cb=xprt_cb, cb_arg=10)
t0 = time.time()
DT = 3.0
while D.updated < 1 and (time.time() - t0 < DT):
    async_cond.acquire()
    async_cond.wait(timeout = 1.0)
check("  async update completed", D.updated == 1)
check("  dir result check", set(async_dlist) == \
                            set(["sync/"+SET_NAME]))
check("  schema name check", set(s.schema_name for s in async_slist) == \
                             set([ 'simple' ]))
for s in async_slist:
    check("  {} data check".format(s.name), s.as_dict() == EXPECTED_DATA)
    # Enable this when unit support is ported from v5
    #check("  {} units check".format(s.name),
    #                [ s.units(i) for i in range(0, len(s)) ] == UNITS)
    pname = s.name.split('/')[0]
    check("  {} producer name check".format(s.name), s.producer_name == pname)
for s in async_slist:
    s.delete()
del async_slist
x.close()
t0 = time.time()
DT = 3.0
while D.connected and (time.time() - t0 < DT):
    async_cond.acquire()
    async_cond.wait(timeout = 1.0)
check("  async close completed", D.connected == 0)


###############################
#     Python sync client     #
###############################
print("=== check synchronous Python ldms client querying ldmsd ===")
x = ldms.Xprt()
# connect
x.connect(host="localhost", port=10002)
# dir
sync_dlist = x.dir()
check("  dir result check", set(s.name for s in sync_dlist) == \
                            set(["async/"+SET_NAME, "sync/"+SET_NAME]))
# lookup (by schema)
sync_slist = x.lookup(SCHEMA_NAME, flags = ldms.LOOKUP_BY_SCHEMA)
check("  lookup result check", set(s.name for s in sync_slist) == \
                               set(["async/"+SET_NAME, "sync/"+SET_NAME]))
check("  schema name check", set(s.schema_name for s in sync_slist) == \
                             set([ 'simple' ]))
# update
for s in sync_slist:
    s.update()
for s in sync_slist:
    check("  {} data check".format(s.name), s.as_dict() == EXPECTED_DATA)
    # Enable this when unit support is ported from v5
    #check("  {} units check".format(s.name),
    #                [ s.units(i) for i in range(0, len(s)) ] == UNITS)
    pname = s.name.split('/')[0]
    check("  {} producer name check".format(s.name), s.producer_name == pname)
for s in sync_slist:
    s.delete()
del(sync_slist)
x.close()

####################################################
#     Python sync client to Python async_server    #
####################################################
print("=== check synchronous Python ldms client querying Python async_server ===")
x = ldms.Xprt()
# connect
x.connect(host="localhost", port=10000)
# dir
sync_dlist = x.dir()
check("  dir result check", set(s.name for s in sync_dlist) == \
                            set(["async/"+SET_NAME]))
# lookup (by name)
tmp = x.lookup("async/"+SET_NAME)
sync_slist = [ tmp ]
check("  lookup result check", set(s.name for s in sync_slist) == \
                               set(["async/"+SET_NAME]))
check("  schema name check", set(s.schema_name for s in sync_slist) == \
                             set([ 'simple' ]))
# update
for s in sync_slist:
    s.update()
for s in sync_slist:
    check("  {} data check".format(s.name), s.as_dict() == EXPECTED_DATA)
    # Enable this when unit support is ported from v5
    #check("  {} units check".format(s.name),
    #                [ s.units(i) for i in range(0, len(s)) ] == UNITS)
    pname = s.name.split('/')[0]
    check("  {} producer name check".format(s.name), s.producer_name == pname)

##############################################
#    Check metric access by various means    #
##############################################
print("=== Check metric data access by various means ===")
s = sync_slist[0]
check("  metric access by name", s["a_str"] == 'abcd')
check("  metric access by ID (index)", s[10] == 10.0)
check("  metric access by slice", s[1:5] == (1,2,3,4))
check("  negative index access", s[-2] == [20.0, 77.0, 22.0, 23.0, 88.0])
check("  MetricArray - list comparison", s[12] == [12, 13, 14, 15, 16])
check("  MetricArray element access", s[12][1] == 13)
check("  MetricArray element access (negative index)", s[12][-1] == 16)
check("  MetricArray elements access (slice)", s[12][2:4] == [14, 15])
# NOTE: For metric data assignment, see `set_test.py`, `async_server.py`, and
#       `sync_server.py`

# Cleanup
for s in sync_slist:
    s.delete()
del(sync_slist)
x.close()

print("=== Terminating subprocesses ===")
agg.terminate()
time.sleep(2) # wait a little so that DISCONNECTED is delivered to async_server
sync_server.terminate()
async_server.terminate()
time.sleep(1)

check("  agg exited cleanly", agg.poll() == 0)
check("  sync_server exited cleanly", sync_server.poll() == 0)
check("  async_server exited cleanly", async_server.poll() == 0)
# NOTE: async_server will exit with non-zero status if there is at least one
#       previously connected transports not being disconnected.

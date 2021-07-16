#!/usr/bin/python3
#
# SYNOPSIS
# --------
# server:
#   ./test.py -s -l XPRT:HOST:PORT
# client:
#   ./test.py -c XPRT:HOST:PORT
#
# DESCRIPTIONS
# ------------
# Immediately update after lookup completed.

import os
import io
import pdb
import sys
import time
import socket
import logging
import argparse as ap
import subprocess as sp
import threading as thread
from queue import Queue
from ovis_ldms import ldms

if __name__ != "__main__":
    raise RuntimeError("This is not a module.")

logging.basicConfig(level=logging.INFO, datefmt="%F %T",
        format="%(asctime)s.%(msecs)d %(levelname)s %(name)s %(message)s")

log = logging.getLogger()

psr = ap.ArgumentParser(description="Python LDMS server-client test", add_help=False)
psr.add_argument("-x", "--xprt", default="sock",
                 help="Transport type (default: sock)")
psr.add_argument("-p", "--port", default="20001",
                 help="Port to listen or to connect (default: 20001)")
psr.add_argument("-h", "--host", default=socket.gethostname(),
                 help="Port to listen or to connect (default: ${HOSTNAME})")
psr.add_argument("-s", "--server", action="store_true",
                 help="Run in `server-mode` (otherwise, `client-mode`")
psr.add_argument("-n", "--num-sets", default=8, type=int,
                 help="The number of sets to test (default=1024)")
psr.add_argument("-i", "--interval", default=1.0, type=float,
                 help="Interval (1.0 sec)")
psr.add_argument("-l", "--listen", metavar='XPRT:HOST:PORT', default=None, type=str,
                 help="Listen to 'XPRT:HOST:PORT' (take precedence over -x,-p,-h)")
psr.add_argument("-c", "--connect", metavar='XPRT:HOST:PORT', default=None, type=str,
                 help="Connect to the given 'XPRT:HOST:PORT' (overrides -x,-p,-h)")
psr.add_argument("-o", "--offset", metavar="FLOAT_SEC", default=0.0, type=float,
                 help="Update offset (default: 0.00)")
psr.add_argument("-?", "--help", action="help",
                 help="Show help message")
g = psr.parse_args()
g.num_metrics = 16
if g.server and g.offset == 0:
    g.offset = 0.2
ldms.init(g.num_sets * 2048) # 2K/set should be plenty

g.sets = dict()
g.prdcr_sets = dict()
g.cond = thread.Condition()
g.num_lookups = 0
g.done_lookup = False
g.num_updates = 0

# metrics: idx, metric_1, metric_2, ...
g.schema = ldms.Schema(name = "test_schema",
                       metric_list = [
                           ("idx", "int32")
                       ] + [
                           ("metric_{}".format(i+1), "int32") \
                                  for i in range(g.num_metrics)
                       ])

class PrdcrSet(object):
    LOOKUP = "LOOKUP"
    UPDATE = "UPDATE"
    READY  = "READY"

    """Mimic producer set in ldmsd"""
    def __init__(self, lset):
        self.lset = lset
        self.state = PrdcrSet.LOOKUP

    def update_cb(self, lset, flags, args):
        log.info("{} update completed".format(self.lset.name))
        self.state = self.READY
        pass

    def update(self):
        assert(self.state != self.UPDATE)
        self.state = self.UPDATE
        log.info("updating {}".format(self.lset.name))
        self.lset.update(self.update_cb, None)

def sample(s):
    s.transaction_begin()
    t = int(time.time())
    m0 = s[0]
    for i in range(g.num_metrics):
        s[i+1] = t + m0 + i
    s.transaction_end()

def verify_set(s):
    if not s.is_consistent:
        return 0
    m0 = s[0]
    local_t = int(time.time())
    t = s[1] - s[0]
    if local_t - t > 10: # lag more than 10 sec
        raise RuntimeError("local_t({}) - t({}) > 10".format(local_t, t))
    for i in range(g.num_metrics):
        assert(s[i+1] == t + m0 + i)
    return 1

def server_cb(x, ev, arg):
    pass # no-op

def listen_cb(x, ev, arg):
    pass # no-op

def interval_block(interval, offset):
        t0 = time.time()
        t1 = (t0 + interval)//interval*interval + offset
        dt = t1 - t0
        time.sleep(dt)

def server_proc():
    if g.listen:
        g.xprt, g.host, g.port = g.listen.split(":")
    g.x = ldms.Xprt(name=g.xprt)
    # create, initialize and publish sets
    for i in range(g.num_sets):
        name = "set_{:06d}".format(i+1)
        s = ldms.Set(name, g.schema)
        s[0] = i+1
        sample(s)
        s.publish()
        g.sets[s.name] = s

    # listen (async)
    g.x.listen(host=g.host, port=g.port, cb=server_cb, cb_arg=None)

    # periodically sample sets
    while True:
        interval_block(g.interval, g.offset)
        for s in g.sets.values():
            sample(s)

def client_lookup_cb(x, status, more, lset, arg):
    log.info("{} lookup completed".format(lset.name))
    g.sets[lset.name] = lset
    pset = PrdcrSet(lset)
    g.prdcr_sets[lset.name] = pset
    pset.update()
    g.num_lookups += 1
    if g.num_lookups == g.num_sets:
        g.done_lookup = True
        g.cond.acquire()
        g.cond.notify()
        g.cond.release()

def client_dir_cb(x, status, dir_data, arg):
    for d in dir_data.set_data:
        x.lookup(name=d.name, cb=client_lookup_cb)

def on_client_connected(x, ev, arg):
    x.dir(cb = client_dir_cb)

def on_client_rejected(x, ev, arg):
    log.error("Rejected")

def on_client_error(x, ev, arg):
    log.error("Connect error")

def on_client_disconnected(x, ev, arg):
    log.error("Disconnected")

def on_client_recv(x, ev, arg):
    log.error("Receiving unexpected message ...")

def on_client_send_complete(x, ev, arg):
    #log.error("Unexpected send completion")
    pass

EV_TBL = {
        ldms.EVENT_CONNECTED: on_client_connected,
	ldms.EVENT_REJECTED: on_client_rejected,
	ldms.EVENT_ERROR: on_client_error,
	ldms.EVENT_DISCONNECTED: on_client_disconnected,
	ldms.EVENT_RECV: on_client_recv,
	ldms.EVENT_SEND_COMPLETE: on_client_send_complete,
    }

def client_cb(x, ev, arg):
    fn = EV_TBL.get(ev.type)
    assert(fn != None)
    fn(x, ev, arg)

def client_update_cb(lset, flags, arg):
    g.num_updates += 1
    assert(flags == 0)
    if g.num_updates == g.num_sets:
        g.cond.acquire()
        g.cond.notify()
        g.cond.release()

def client_proc():
    # async listen
    if g.listen:
        _xprt, _host, _port = g.listen.split(":")
        _x = ldms.Xprt(name = _xprt)
        _x.listen(host=_host, port=_port, cb=listen_cb, cb_arg=None)
    if g.connect:
        g.xprt, g.host, g.port = g.connect.split(":")
    # async connect
    g.x = ldms.Xprt(name=g.xprt)
    g.x.connect(host=g.host, port=g.port, cb=client_cb, cb_arg=None)
    # wait for lookup
    g.cond.acquire()
    while not g.done_lookup:
        g.cond.wait()
    g.cond.release()
    assert(g.num_lookups == g.num_sets)
    log.info("all lookup completed")

    # periodically update sets
    while False:
        # This is an old routine
        interval_block(g.interval, g.offset)
        g.num_updates = 0
        for s in g.sets.values():
            s.update(client_update_cb, None)
        # wait for update completions
        g.cond.acquire()
        while g.num_updates < g.num_sets:
            g.cond.wait()
        g.cond.release()
        v = 0
        n_consistent = sum( s.is_consistent for s in g.sets.values() )
        log.info("{}/{} sets consistent".format(n_consistent, g.num_sets))
        if False:
            for s in g.sets.values():
                v += verify_set(s)
            log.info("{}/{} sets verified".format(v, g.num_sets))

if g.server:
    server_proc()
else:
    client_proc()

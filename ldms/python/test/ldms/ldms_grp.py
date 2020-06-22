#!/usr/bin/env python3
#
# Test ldms_grp capability
# ------------------------
#
# When the script is running with "main" mode (default), it will spawn "samp"
# and "agg" and test as follows:
# - samp
#   - samp creates 4 LDMS sets and 1 grp.
#   - samp keeps updating metric values based on BASE_VALUE
# - agg
#   - lookup `grp` and verify that agg got all members.
#   - update `grp` and verify that all members got updated.
#   - tell `samp` to change BASE_VALUE to 10.
#   - update `grp` and verify that all members reflect the change of BASE_VALUE.
#   - create `agg_grp`, a new group on the agg which contains half the members
#     of `grp`.
#   - tell `samp` to change the BASE_VALUE to 20.
#   - update `agg_grp`, and verify that ONLY the members in `agg_grp` got
#     updated to the new BASE_VALUE.
#   - tell `samp` to remove a member from `grp`.
#   - update `grp` and verify that the member data has changed.
#   - tell `samp` to change the BASE_VALUE to 30.
#   - update `grp` and verify that only member sets got updated to the new
#     BASE_VALUE.
#
# The result verification is in "agg" part. The "main" process will print
# results it got from "agg" and will `exit(-1)` if "agg" exited with an error.

import os
import re
import sys
import json
import time
import ctypes
import argparse as ap
import subprocess as sp
import threading as thread
from ovis_ldms import ldms

libc = ctypes.CDLL(None)
# prctl(PR_SET_PDEATHSIG, SIGHUP)
#   we will receive SIGHUP on the death of parent proc
libc.prctl(1, 1)

ldms.init(128*1024*1024)

class Debug(object): pass

D = Debug()

schema = ldms.Schema(name = "counters", metric_list = [
        ( "a", "int64" ),
        ( "b", "int64" ),
    ])

NSETS = 4
SET_NAMES = [ "set{:02d}".format(i) for i in range(0, NSETS) ]
GRP_MEMBERS = SET_NAMES + [ "does_not_exist" ]
DATA_FILE = "data.json"
BASE_VALUE = 1
ASYNC = 0 # asynchronous operation option

SET_NAME_RE = re.compile(r'^set(\d+)$')

BLACK  = "\033[90m"
RED    = "\033[91m"
GREEN  = "\033[92m"
YELLOW = "\033[93m"
BLUE   = "\033[94m"
PURPLE = "\033[95m"
CYAN   = "\033[96m"
GREY   = "\033[97m"
RESET  = "\033[0m"

def ts_str(ts=None):
    if ts is None:
        ts = time.time()
    usec = int((ts % 1) * 1000000)
    return time.strftime("%Y-%m-%d %F", time.localtime(ts)) + "." + str(usec)

def INFO(*args):
    print(ts_str(), GREEN+"INFO:"+RESET, *args)

def WARN(*args):
    print(ts_str(), YELLOW+"ERROR:"+RESET, *args)

def ERROR(*args):
    print(ts_str(), RED+"ERROR:"+RESET, *args)

def ERROR_RAISE(*args):
    """Print error message and raise RuntimeError with the message"""
    print(ts_str(), RED+"ERROR:"+RESET, *args)
    raise RuntimeError(" ".join(map(str, args)))

def ASSERT(cond, desc):
    if not cond:
        ERROR(desc, ":", RED+"failed"+RESET)
        raise RuntimeError(desc + ": failed")
    INFO(desc, ":", GREEN+"passed"+RESET)

def verify_set(_set, base_value = None):
    global BASE_VALUE
    if base_value is None:
        base_value = BASE_VALUE
    if type(_set) == ldms.Grp:
        return # ignore group
    m = SET_NAME_RE.match(_set.name)
    if not m:
        ERROR_RAISE("Unknown set: {}".format(_set.name))
    i = int(m.group(1))
    expected_values = ( base_value + i, base_value + i + 1 )
    ASSERT(expected_values == _set[:], "{} metric check".format(_set.name))

def verify_sets(sets, base_value=None):
    for s in sets:
        verify_set(s, base_value)

def passive_xprt_cb(x, ev, arg):
    global D
    if ev.type != ldms.EVENT_RECV:
        return
    # Expect JSON format
    global BASE_VALUE
    msg = json.loads(ev.data)
    BASE_VALUE = msg.get("BASE_VALUE", BASE_VALUE)
    grp_rm = msg.get("grp_rm")
    if grp_rm:
        D.grp.transaction_begin()
        for name in grp_rm:
            D.grp.remove(name)
        D.grp.transaction_end()

def do_listen(port):
    lx = ldms.Xprt()
    lx.listen(port=port, cb=passive_xprt_cb, cb_arg=lx)

def samp_update(sets):
    i = BASE_VALUE
    for s in sets:
        s.transaction_begin()
        s[:] = (i, i+1)
        s.transaction_end()
        i += 1

def do_samp(samp_port, *args, **kwargs):
    global D
    # samp routine
    D.grp = grp = ldms.Grp(name="grp")
    grp.publish()
    sets = [ ldms.Set(name=s, schema=schema) for s in SET_NAMES ]
    samp_update(sets)
    for s in sets:
        s.publish()
    grp.transaction_begin()
    for m in GRP_MEMBERS:
        grp.add(m)
    grp.transaction_end()
    do_listen(samp_port)
    while True:
        time.sleep(1)
        samp_update(sets)

lookup_done = 0
lookup_cond = thread.Condition()

def lookup_cb(x, status, more, lset, ctxt, *args, **kwargs):
    global D, lookup_done
    ASSERT(ctxt == D, "lookup callback context check")
    if not status:
        D.sets.append(lset)
    if not more:
        lookup_cond.acquire()
        lookup_done = 1
        lookup_cond.notify()
        lookup_cond.release()

def do_lookup(x):
    global D, lookup_done, ASYNC, SET_NAMES
    INFO("==== lookup ====")
    if not ASYNC:
        D.sets = x.lookup("grp")
        return D.sets
    D.sets = sets = []
    lookup_done = 0
    x.lookup("grp", cb=lookup_cb, cb_arg=D)
    lookup_cond.acquire()
    if not lookup_done:
        lookup_cond.wait(timeout = 4)
        if not lookup_done:
            ERROR_RAISE("lookup timeout")
    lookup_cond.release()
    # verify set names
    D._expect = _expect = SET_NAMES + ["grp"]
    _expect.sort()
    D._names = _names = [ s.name for s in sets ]
    _names.sort()
    ASSERT(_names == _expect, "lookup results check")
    for s in sets:
        ASSERT(s.is_consistent == False, "{} is inconsistent right after lookup".format(s.name))
    return sets

update_done = 0
update_cond = thread.Condition()

def update_cb(_set, _flags, _arg):
    global D, update_cond, update_done
    D.updated_sets.append(_set)
    if _set is _arg:
        update_cond.acquire()
        update_done = 1
        update_cond.notify()
        update_cond.release()

def do_update(g, expected_updates = None):
    global D, update_done, ASYNC
    if not ASYNC:
        g.update()
        return
    D.updated_sets = list()
    update_done = 0
    g.update(update_cb, g)
    update_cond.acquire()
    if not update_done:
        update_cond.wait(timeout = 4)
        if not update_done:
            ERROR_RAISE("update timeout")
    update_cond.release()
    INFO("updated sets:", [ s.name for s in D.updated_sets ])
    if expected_updates:
        ASSERT(expected_updates == D.updated_sets, "update callbacks check")

def agg_change_base_value(x, new_base_value):
    # Change agg BASE_VALUE and tell samp to also change its BASE_VALUE
    global BASE_VALUE
    BASE_VALUE = new_base_value
    obj = {"BASE_VALUE": BASE_VALUE}
    msg = json.dumps(obj)
    x.send(msg.encode())

def do_agg_grp(x):
    global D
    INFO("==== agg_grp (grp on aggregator) ====")
    D.agg_grp = agg_grp = ldms.Grp(name="agg_grp")
    agg_grp.transaction_begin()
    agg_grp.add("set01")
    agg_grp.add("set03")
    agg_grp.transaction_end()
    INFO(str(agg_grp))
    ASSERT(set(agg_grp) == set(["set01", "set03"]), "agg_grp members check")
    agg_change_base_value(x, 20)
    time.sleep(2)
    sets = D.sets
    a_sets = [ s for s in sets if s.name in [ "set01", "set03" ] ]
    a_sets.append(agg_grp)
    b_sets = [ s for s in sets if s.name in [ "set00", "set02" ] ]
    do_update(agg_grp, a_sets)
    verify_sets(a_sets, 20)
    verify_sets(b_sets, 10)

def do_remote_member_change():
    global D
    INFO("==== Remote member change test ====")
    x = D.x
    grp = D.grp
    sets = D.sets
    x.send(json.dumps({"grp_rm": ["set00"]}).encode())
    time.sleep(1)
    do_update(grp, sets) # This update fetch only the data section (cheap). The
                         # member information (relatively bigger) lives in
                         # metadata section.
    verify_sets(sets)
    do_update(grp, sets) # This update will also fetch metadata as it now knows
                         # that meta_gn has changed. The membership before
                         # metadata fetching stays the same, hence it still
                         # updates `set00`.
    verify_sets(sets)
    # After the 2nd update (metadata fetch), the membership should change.
    INFO(str(grp))
    ASSERT(set(grp) == set(["does_not_exist", "set01", "set02", "set03"]), "verify members")
    agg_change_base_value(x, 30)
    time.sleep(1)
    a_sets = [ s for s in sets if s.name != "set00" ]
    b_sets = [ s for s in sets if s.name == "set00" ]
    do_update(grp, a_sets)
    verify_sets(a_sets, 30)
    verify_sets(b_sets, 20)

def do_agg(samp_port, agg_port, *args, **kwargs):
    # agg-1 routine
    D.x = x = ldms.Xprt()
    x.connect(host="localhost", port = samp_port)
    agg_change_base_value(x, 1) # initial
    time.sleep(1)
    D.dlist = dlist = x.dir()
    # lookup
    sets = do_lookup(x)
    grps = [ s for s in sets if type(s) == ldms.Grp ]
    ASSERT(len(grps) == 1, "expect 1 group")
    D.grp = grp = grps[0]
    INFO("==== 1st grp update ====")
    do_update(grp, [ grp ]) # The first update will update only grp.
                            # This is because grp is inconsistent.
    INFO("==== 2nd grp update ====")
    do_update(grp, D.sets) # This update will update all members.
    verify_sets(sets)
    # Change the BASE_VALUE and update
    INFO("==== change base value, then grp update ====")
    agg_change_base_value(x, 10)
    time.sleep(2)
    do_update(grp, D.sets)
    verify_sets(sets)
    do_agg_grp(x)
    do_remote_member_change()

def do_main(parsed_args):
    global ASYNC
    a = parsed_args
    opts = [ "-S", str(a.samp_port), "-A", str(a.agg_port) ]
    if ASYNC:
        opts.append("-a")
    samp_cmd = [ "/usr/bin/python3", sys.argv[0], "-m", "samp" ] + opts
    agg_cmd =  [ "/usr/bin/python3", sys.argv[0], "-m", "agg"  ] + opts
    D.samp_proc = samp_proc = sp.Popen(samp_cmd, bufsize = 4096, stdin=sp.PIPE,
                                       stdout=sp.PIPE, stderr=sp.PIPE)
    D.agg_proc = agg_proc = sp.Popen(agg_cmd, bufsize = 4096, stdin=sp.PIPE,
                                     stdout=sp.PIPE, stderr=sp.PIPE)
    agg_proc.wait()
    out = agg_proc.stdout.read()
    print(out.decode())
    samp_proc.terminate()
    if agg_proc.poll() != 0:
        sys.exit(-1)

if __name__ == "__main__":
    p = ap.ArgumentParser(description = "A test program for LDMS group. "
            "See the documentation in the source code for more information.")
    p.add_argument("--mode", "-m", type=str, choices=["main", "samp", "agg"],
                   default="main",
                   help="main (default), samp, or agg")
    p.add_argument("--samp-port", "-S", default=10000, type=int,
                   help="sampler port")
    p.add_argument("--agg-port", "-A", default=10001, type=int,
                   help="agg port")
    p.add_argument("--async", "-a", action="store_true",
                   help="Using asynchronous model (callbacks)")
    args = p.parse_args()
    ASYNC = args.async
    if args.mode == "samp":
        do_samp(**vars(args))
    elif args.mode == "agg":
        do_agg(**vars(args))
    elif args.mode == "main":
        do_main(args)
    else:
        ERROR("Unknown mode: {}".format(args.mode))
        sys.exit(-1)

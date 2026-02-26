#!/usr/bin/env python3

import time
import json

from ovis_ldms import ldms

class Debug(object): pass

D = Debug()

ldms.init(16 * 1024 * 1024)

x = ldms.Xprt()

x.connect("localhost", 4411)

_dirs = x.dir()

_sets = [ x.lookup(d.name) for d in _dirs ]

s0 = _sets[0]

def perf_detail(s):
    l0 = [ o for o in s['counters'] ]
    l1 = [ o for o in s['scaled_counters'] ]
    _ret = list()
    ts = s.transaction_timestamp
    _ts = ts['sec'] + ts['usec']*1e-6
    print(f"ts: {_ts}")
    for rec in l0+l1:
        _counters = list(rec[2])
        _name = rec[0]
        _ret.append( (_name, _counters) )
        print(f"  {_name}: {_counters}")
    return _ret

def perf_summary(s):
    # s is the set
    l0 = [ o for o in s['counters'] ]
    l1 = [ o for o in s['scaled_counters'] ]
    _ret = list()
    ts = s.transaction_timestamp
    _ts = ts['sec'] + ts['usec']*1e-6
    print(f"ts: {_ts}")
    for rec in l0+l1:
        _sum = sum(rec[2])
        _name = rec[0]
        _ret.append( (_name, _sum) )
        print(f"  {_name}: {_sum}")
    return _ret

def summary_diff(before, after):
    _ret = list()
    print("diff")
    for b, a in zip(before, after):
        d = a[1] - b[1]
        _name = a[0]
        assert( a[0] == b[0] )
        print(f"  {_name}: {d}")
        _ret.append( (a[0], d) )
    return _ret

def detail_diff(before, after):
    _ret = list()
    print("detail diff")
    for b, a in zip(before, after):
        d = [ _a - _b for _a, _b in zip(a[1], b[1]) ]
        _name = a[0]
        assert( a[0] == b[0] )
        print(f"  {_name}: {d}")
        _ret.append( (a[0], d) )
    return _ret

def do_perf(s, sec = 1):
    s.update()
    s.update()
    D.d0 = d0 = perf_detail(s)
    D.r0 = r0 = perf_summary(s)
    time.sleep(1)
    s.update()
    D.d1 = d1 = perf_detail(s)
    D.r1 = r1 = perf_summary(s)

    D.dd = dd = detail_diff(d0, d1)
    D.sd = sd = summary_diff(r0, r1)

time.sleep(2) # wait for idling ... (to compare w/ perf)
do_perf(s0)

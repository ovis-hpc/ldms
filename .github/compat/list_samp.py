#!/usr/bin/python3

import os
import sys
import time
import logging

from ovis_ldms import ldms

LOG_FMT = "%(asctime)s.%(msecs)03d %(levelname)s %(module)s %(message)s"
logging.basicConfig(datefmt="%F-%T", format=LOG_FMT, level=logging.INFO)
log = logging.getLogger()

ldms.init(16*1024*1024)

SCHEMA = ldms.Schema("test", metric_list = [
        ("x", ldms.V_S64),
        ("y", ldms.V_S64),
        ("z", ldms.V_S64),
        ("list", ldms.V_LIST, 2048),
        ("dir",  ldms.V_LIST, 4096),
    ])

class Global(object): pass

G = Global()

class Seq(object):
    def __init__(self, start):
        self.x = start - 1
    def next(self):
        self.x += 1
        return self.x

def list_append(mlst, data):
    # mlst is MetricList object
    # data is [ (type, obj) ]
    for t, v in data:
        o = mlst.append(t, v)
        if t == ldms.V_LIST:
            list_append(o, v)

def list_update(mlst, data):
    for m, (t, v) in zip(mlst, data):
        m.set(v)

def gen_data(seed):
    seq = Seq(seed)
    return [
        (ldms.V_S64, seq.next()), # x
        (ldms.V_S64, seq.next()), # y
        (ldms.V_S64, seq.next()), # z
        (ldms.V_LIST, [ # list
            (ldms.V_CHAR, [ 'a', 'b' ][seq.next()%2]), # char, 'a' or 'b'
            (ldms.V_U8, seq.next()), # u8
            (ldms.V_S8, seq.next()), # s8
            (ldms.V_U16, seq.next()), # u16
            (ldms.V_S16, seq.next()), # s16
            (ldms.V_U32, seq.next()), # u32
            (ldms.V_S32, seq.next()), # s32
            (ldms.V_U64, seq.next()), # u64
            (ldms.V_S64, seq.next()), # s64
            (ldms.V_F32, seq.next()), # float
            (ldms.V_D64, seq.next()), # double
            (ldms.V_CHAR_ARRAY, str(seq.next())), # str
            (ldms.V_U8_ARRAY,  tuple(seq.next() for i in range(3)) ), # u8
            (ldms.V_S8_ARRAY,  tuple(seq.next() for i in range(3)) ), # s8
            (ldms.V_U16_ARRAY, tuple(seq.next() for i in range(3)) ), # u16
            (ldms.V_S16_ARRAY, tuple(seq.next() for i in range(3)) ), # s16
            (ldms.V_U32_ARRAY, tuple(seq.next() for i in range(3)) ), # u32
            (ldms.V_S32_ARRAY, tuple(seq.next() for i in range(3)) ), # s32
            (ldms.V_U64_ARRAY, tuple(seq.next() for i in range(3)) ), # u64
            (ldms.V_S64_ARRAY, tuple(seq.next() for i in range(3)) ), # s64
            (ldms.V_F32_ARRAY, tuple(seq.next() for i in range(3)) ), # float
            (ldms.V_D64_ARRAY, tuple(seq.next() for i in range(3)) ), # double
        ]),
        (ldms.V_LIST, [ # dir
            (ldms.V_CHAR_ARRAY, "/"),
            (ldms.V_LIST, [
                (ldms.V_CHAR_ARRAY, "bin/"), # dir name
                (ldms.V_LIST, [   # dir content
                    (ldms.V_CHAR_ARRAY, "bash"), # file
                    (ldms.V_CHAR_ARRAY, "ls"),   # file
                ]),
                (ldms.V_CHAR_ARRAY, "var/"), # dir name
                (ldms.V_LIST, [
                    (ldms.V_CHAR_ARRAY, "run/"), # dir name
                    (ldms.V_LIST, [
                        (ldms.V_CHAR_ARRAY, "sshd.pid"),
                        (ldms.V_CHAR_ARRAY, "lock/"),
                        (ldms.V_LIST, [
                            (ldms.V_CHAR_ARRAY, "file")
                        ]),
                    ]),
                    (ldms.V_CHAR_ARRAY, "log/"), # dir name
                    (ldms.V_LIST, [
                        (ldms.V_CHAR_ARRAY, "sshd.log"),
                    ]),
                ]),
            ]),
        ]),
    ]

def update_set(_set, i):
    x, y, z, lst, dr = gen_data(i)
    _set.transaction_begin()
    _set['x'] = x[1]
    _set['y'] = y[1]
    _set['z'] = z[1]
    mlst = _set['list']
    mdr  = _set['dir']
    if len(mlst): # only update the values if list has been populated
        list_update(mlst, lst[1])
    else:
        list_append(mlst, lst[1])
    # append `dir` values
    if len(mdr) == 0:
        list_append(mdr, dr[1])
    _set.transaction_end()

set1 = ldms.Set("ovis4/list", SCHEMA)
set1.publish()
update_set(set1, 1)

x = ldms.Xprt(name="sock")
def listen_cb(ep, ev, arg):
    log.debug("{}: {}".format(ev.type.name, ep))
    G.ep = ep
    G.ev = ev

rc = x.listen(host="0.0.0.0", port=10003, cb=listen_cb)
while True:
    time.sleep(1)

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

SCHEMA = ldms.Schema("compat", metric_list = [
        ("u8", ldms.V_U8),
        ("u16", ldms.V_U16),
        ("u32", ldms.V_U32),
        ("u64", ldms.V_U64),
        ("s8", ldms.V_S8),
        ("s16", ldms.V_S16),
        ("s32", ldms.V_S32),
        ("s64", ldms.V_S64),
        ("f32", ldms.V_F32),
        ("d64", ldms.V_D64),
        ("char_array", ldms.V_CHAR_ARRAY, 16),
        ("u8_array", ldms.V_U8_ARRAY, 4),
        ("u16_array", ldms.V_U16_ARRAY, 4),
        ("u32_array", ldms.V_U32_ARRAY, 4),
        ("u64_array", ldms.V_U64_ARRAY, 4),
        ("s8_array", ldms.V_S8_ARRAY, 4),
        ("s16_array", ldms.V_S16_ARRAY, 4),
        ("s32_array", ldms.V_S32_ARRAY, 4),
        ("s64_array", ldms.V_S64_ARRAY, 4),
        ("f32_array", ldms.V_F32_ARRAY, 4),
        ("d64_array", ldms.V_D64_ARRAY, 4),
        ("list", ldms.V_LIST, 2048)
    ])

class Global(object): pass

G = Global()

class Seq(object):
    def __init__(self, start):
        self.x = start - 1
    def next(self):
        self.x += 1
        return self.x

def update_set(_set, i):
    seq = Seq(i)
    _set.transaction_begin()
    _set['char_array'] = "hello, world"
    _set['u8'] = seq.next()
    _set['u8_array'] = (1, 2, 3, 4)
    _set['u16_array'] = (1, 2, 3, 4)
    _set['u32_array'] = (1, 2, 3, 4)
    _set['u64_array'] = (1, 2, 3, 4)
    _set['s8_array'] = (-1, -2, 3, 4)

    _set['s16_array'] = (-1, -2, 3, 4)
    _set['s32_array'] = (-1, -2, 3, 4)
    _set['s64_array'] = (-1, -2, 3, 4)
    _set['f32_array'] = (-1, -2, 3, 4)
    _set['d64_array'] = (-1, -2, 3, 4)

    _set['list'].append(ldms.V_U64, seq.next())
    _set['list'].append(ldms.V_U64, seq.next())
    _set['list'].append(ldms.V_U64, seq.next())
    _set['list'].append(ldms.V_U64, seq.next())
    _set.transaction_end()

set1 = ldms.Set("ovis4/compat", SCHEMA)
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

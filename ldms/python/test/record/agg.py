#!/usr/bin/python3 import os
import sys
import logging

from ovis_ldms import ldms

from common import *

ldms.init(16*1024*1024)

def listen_cb(x, ev, arg):
    # no-op
    pass

lx = ldms.Xprt("sock")
lx.listen(port=AGG_PORT, cb=listen_cb)

cx = ldms.Xprt("sock")
cx.connect("localhost", port=SAMP_PORT)

_dir = cx.dir()
_sets = [ cx.lookup(d.name) for d in _dir ]
for s in _sets:
    s.update()

print(s.json(indent=2))

#!/usr/bin/python3 import os
import sys
import time
import logging

from ovis_ldms import ldms

from common import *

ldms.init(16*1024*1024)

_set = ldms.Set("the_set", SCHEMA)
_lst = _set["device_list"]

sample_set(_set, 0)
_set.publish()

def listen_cb(x, ev, arg):
    # no-op
    pass

xprt = ldms.Xprt("sock")
xprt.listen(port=SAMP_PORT, cb=listen_cb)

if not sys.flags.interactive:
    while True:
        time.sleep(1)

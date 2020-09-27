#!/usr/bin/python3
#
# An example of LDMS provider using Python with asynchronous programming style.
#
# The script listens on port 10000 and create a set using SCHEMA defined in
# `set_test.py` module. The script run indefinitely until SIGTERM is received.
#
# This script provides `async/the_set` LDMS set.

import os
import sys
import time
import ctypes
import signal

from ovis_ldms import ldms
import set_test

ldms.init(16*1024*1024)

# Request SIGHUP our process when parent exited
libc = ctypes.CDLL(None)
# prctl(PR_SET_PDEATHSIG, SIGHUP)
libc.prctl(1, 1)

# Create a transport
lx = ldms.Xprt()
print("listening xprt:", hex(id(lx)), flush=True)

xset = set() # transport collection

# SIGTERM handler
def on_SIGTERM(*args):
    global xset
    if xset: # If there is at least one connected transports, exit with an error.
        print("XPRT collection not empty:", xset, flush=True)
        os._exit(1)
    print("async_server terminated", flush=True)
    os._exit(0)

# register SIGTERM handler
signal.signal(signal.SIGTERM, on_SIGTERM)


def listen_cb(x, ev, arg):
    global xset
    # x is the new transport for CONNECTED event
    print("xprt:", hex(id(x)), "event:", ev.type, flush=True)
    if ev.type == ldms.EVENT_CONNECTED:
        # asserting that the newly connected transport is a new one.
        assert(x.ctxt == None)
        x.ctxt = "some_context {}".format(x)
        xset.add(x)
    elif ev.type == ldms.EVENT_DISCONNECTED:
        # also asserting that this is the transport from the earlier CONNECTED
        # event.
        assert(x.ctxt == "some_context {}".format(x))
        xset.remove(x)

# Listen with callback
lx.listen(port=10000, cb=listen_cb, cb_arg=None)

# create the set
set_name = "async/" + set_test.SET_NAME
lset = ldms.Set(name=set_name, schema=set_test.SCHEMA)
lset.producer_name = "async"
lset.publish()

# populate set data
set_test.populate_data(lset) # see `set_test.py`

# run indefinitely until SIGTERM is received
while True and not sys.flags.interactive:
    time.sleep(1)

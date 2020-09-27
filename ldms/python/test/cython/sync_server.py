#!/usr/bin/python3
#
# An example of LDMS provider using Python with synchronous programming style.
#
# The script listens on port 10001 and create a set using SCHEMA defined in
# `set_test.py` module. The script run indefinitely until SIGTERM is received.
#
# This script provides `sync/the_set` LDMS set.

import os
import sys
import time
import ctypes
import signal

from ovis_ldms import ldms
import set_test

ldms.init(16*1024*1024)

def on_SIGTERM(*args):
    os._exit(0)

signal.signal(signal.SIGTERM, on_SIGTERM)

# Request SIGHUP our process when parent exited
libc = ctypes.CDLL(None)
# prctl(PR_SET_PDEATHSIG, SIGHUP)
libc.prctl(1, 1)

# create the transport
lx = ldms.Xprt()

# listen
lx.listen(port=10001)

# create and publish set
set_name = "sync/" + set_test.SET_NAME
lset = ldms.Set(name=set_name, schema=set_test.SCHEMA)
lset.producer_name = "sync"
lset.publish()

# populate set data
set_test.populate_data(lset)

while True:
    x = lx.accept()
    # client will close the connection

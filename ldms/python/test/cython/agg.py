#!/usr/bin/python3

# This is a wrapper of `ldmsd -F -c ldmsd.cfg` aggregator that requests SIGHUP
# to be sent to `ldmsd` when the parent process (`test.py`) terminated.

import os
import ctypes

libc = ctypes.CDLL(None)
# prctl(PR_SET_PDEATHSIG, SIGHUP)
libc.prctl(1, 1)
os.execlp("ldmsd", "ldmsd", "-F", "-c", "ldmsd.cfg")

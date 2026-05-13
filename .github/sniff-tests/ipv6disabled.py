#!/usr/bin/env python3

import sys
import errno
from ovis_ldms import ldms

e = None
x = ldms.Xprt()
try:
    x.listen("::1", 10000)
except ConnectionError as _e:
    e = _e

# If IPv6 is disabled, we expect listen to failed with E2BIG
if e:
    if e.errno == errno.E2BIG:
        print("IPv6 is disabled")
        sys.exit(0)
    print(f"Unexpected error: {e}")
    sys.exit(-1)

print("IPv6 is enabled")
sys.exit(-1)

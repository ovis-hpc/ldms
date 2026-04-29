#!/usr/bin/env python3
import os

from ovis_ldms import ldms

HOST = os.environ.get("LDMS_HOST", "localhost")
PORT = os.environ.get("LDMS_PORT", "9411")
XPRT = os.environ.get("LDMS_XPRT", "sock")

ldms.init(16*1024*1024)
x = ldms.Xprt(XPRT, "none")
x.connect(host=HOST, port=PORT)
x.msg_publish("foo", "bar")

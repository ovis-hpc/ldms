#!/usr/bin/env python3
import os
import time

from ovis_ldms import ldms

HOST = os.environ.get("LDMS_HOST", "localhost")
PORT = os.environ.get("LDMS_PORT", "9411")
XPRT = os.environ.get("LDMS_XPRT", "sock")

ldms.init(16*1024*1024)
x = ldms.Xprt(XPRT, "none")
x.connect(host=HOST, port=PORT)

mc = ldms.MsgClient("foo", is_regex=False)
x.msg_subscribe("foo", is_regex=False)

t = time.time()
t_fin = t + 5 # 5 sec in the future

while t < t_fin:
    m = mc.get_data()
    if m:
        break
    time.sleep(0.1)
    t = time.time()

if m:
    print(f"data: {m.data}")
else:
    raise RuntimeError("no data")

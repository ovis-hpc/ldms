#!/usr/bin/env python3

import sys
import time
import argparse as ap
from ovis_ldms import ldms

ldms.init(16*1024*1024)

p = ap.ArgumentParser()
p.add_argument("--host", "-H", type=str, default="localhost")
p.add_argument("--port", "-P", type=int, default=411)
p.add_argument("--tag", "-T", type=str, default="TAG")
p.add_argument("--wait", "-W", type=int, default=5)

args = p.parse_args()

mc = ldms.MsgClient(match=args.tag, is_regex=False)

x = ldms.Xprt("sock")
x.connect(host=args.host, port=args.port)

x.msg_subscribe(match=args.tag, is_regex=False)

ts = time.time()
end_ts = ts + args.wait

m = None

while ts < end_ts:
    m = mc.get_data()
    if m:
        break
    time.sleep(0.1)
    ts = time.time()

if m:
    print(f"{m.name}: {m.data}")
    sys.exit(0)
else:
    print(f"None")
    sys.exit(-1)

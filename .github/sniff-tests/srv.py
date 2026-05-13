#!/usr/bin/env python3
import os
import time
import subprocess as sp
import argparse as ap
from ovis_ldms import ldms

ldms.init(16*1024*1024)

p = ap.ArgumentParser()
p.add_argument("--host", "-H", type=str)
p.add_argument("--port", "-P", type=int, default=411)

args = p.parse_args()

x = ldms.Xprt("sock")
x.listen(host=args.host, port=args.port)

ss = sp.getoutput(f"ss -tlnp | grep 'pid={os.getpid()},'")
print(f"{ss}")

sch = ldms.Schema(name="SimpleSchema", metric_list=[
                    ("x", "float"),
                    ("y", "float"),
      ])

s = ldms.Set(name="simple_set", schema=sch)
s.publish()
s.transaction_begin()
s[0] = 1.0
s["y"] = 2.0
s.transaction_end()

time.sleep(1)

while True:
    time.sleep(1)

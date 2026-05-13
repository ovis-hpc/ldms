#!/usr/bin/env python3
import sys
import argparse as ap
from ovis_ldms import ldms

ldms.init(16*1024*1024)

p = ap.ArgumentParser()
p.add_argument("--host", "-H", type=str, required=True)
p.add_argument("--port", "-P", type=int, default=411)

args = p.parse_args()

x = ldms.Xprt("sock")
try:
    x.connect(host=args.host, port=args.port)
except Exception as e:
    print(f"connect error: {e}")
    sys.exit(-1)

dirs = x.dir()

sets = [ x.lookup(d.name) for d in dirs ]

for s in sets:
    s.update()

for s in sets:
    print(f"{s.name}({s.schema_name}): {s.as_dict()}")

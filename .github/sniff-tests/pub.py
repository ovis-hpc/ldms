#!/usr/bin/env python3

import argparse as ap
from ovis_ldms import ldms

ldms.init(16*1024*1024)

p = ap.ArgumentParser()
p.add_argument("--host", "-H", type=str, default="localhost")
p.add_argument("--port", "-P", type=int, default=411)
p.add_argument("--tag", "-T", type=str, default="TAG")
p.add_argument("--data", "-D", type=str, default="DATA")

args = p.parse_args()

x = ldms.Xprt("sock")
x.connect(host=args.host, port=args.port)

x.msg_publish(args.tag, args.data)

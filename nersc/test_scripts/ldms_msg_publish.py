#!/usr/bin/env python3
import sys
from datetime import datetime, timezone
sys.path.append('/opt/ovis-ldms/lib/python3.6/site-packages')
from ovis_ldms import ldms
now = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%S')
msg = {"message": {"@timestamp":f"{now}","data":{"type":"jss test"},"ndc":{"system":"muller"},"msgtype":"mods-test","host":"muller-mgr"}}
print(f"publishing:  {msg}")
ldms.init(16*1024*1024)
x = ldms.Xprt("sock", "munge", auth_opts={'socket':'/var/run/munge/munge.socket.2'})
x.connect("localhost", "6002")
x.msg_publish("nersc", msg)


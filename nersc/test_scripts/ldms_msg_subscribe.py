#!/usr/bin/env python3
import time
import sys
from ovis_ldms import ldms

mc = ldms.MsgClient(".*", True)

x = ldms.Xprt(name="sock", auth="munge", auth_opts={'socket' : '/var/run/munge/munge.socket.2'})
#x = ldms.Xprt(name="sock", auth="ovis", auth_opts={'conf':'/ldms-auth-omni/omni.ldmsauth.conf'})
x.connect(host="localhost", port=6002)

x.msg_subscribe("nersc", True)

while True:
    d = mc.get_data()
    while d is None:
        time.sleep(0.25)
        d = mc.get_data()
    ts = time.strftime("%F %T") + f".{int( (time.time()%1)*1e6 ):06}"
    print(ts, d.name, ":", d.data)


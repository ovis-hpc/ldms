#!/usr/bin/env python
import sys
from ovis_lib import zap
from array import array

z = zap.zap_GET("sock")
ep = zap.zap_NEW(z)
rc = zap.zap_connect_ez(ep, "localhost:55555")
print rc
data = array('B', "hello")
data.append(0)
# sending "hello\0"
print "sending: ", data
zap.zap_send(ep, data, len(data))

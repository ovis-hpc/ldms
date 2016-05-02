#!/usr/bin/env python
import os
import subprocess
import re

CFG_FILE = "../../test/indy_test/config.sh"

pipe = subprocess.Popen(["/bin/bash"], stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)

pipe.stdin.write("source %s; env | grep BTEST; exit 0;" % CFG_FILE)
pipe.stdin.close()

for _line in pipe.stdout.readlines():
    (_var, _val) = _line.strip().split("=", 1)
    exec("%s = \"%s\"" % (_var, _val))

if __name__ == "__main__":
    for _var in dir():
        if re.match("BTEST_.*", _var):
            exec("_val = %s" % _var)
            print "%s=%s" % (_var, _val)

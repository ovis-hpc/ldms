#!/usr/bin/python3
import os
import io
import re
import sys
import tempfile
import subprocess as sp
import argparse as ap

p = ap.ArgumentParser(description="Print enumerations from given ovis dir")
p.add_argument("ovis_dir", metavar="OVIS_DIR", nargs=1, help="ovis src directory")
#p.add_argument("--prog", "-o", metavar="PROG", type=str, default="prog",
#               help="output enum program")
p.add_argument("--enum-list", "-e", metavar="ENUM_LIST_FILE", type=str, default=None,
               help="File of enum list")
args = p.parse_args()

OVIS_DIR = os.path.realpath(args.ovis_dir[0])
if not os.path.isdir(OVIS_DIR):
    raise RuntimeError("{} is not a directory or not exist".format(OVIS_DIR))

# directory or files to include
HDR_LIST = [
    "lib/src/zap/zap.h",
    "lib/src/zap/sock",
    "lib/src/zap/rdma",
    "ldms/src/core/",
    "ldms/src/ldmsd/",
]

EXCLUDE_PATH_RE = re.compile(r'.*ldms_stream_avro_ser[.]h')

_list = list()
for e in HDR_LIST:
    d = OVIS_DIR + "/" + e
    if not os.path.exists(d):
        raise RuntimeError("{} does not exist".format(d))
    if not os.path.isdir(d):
        _list.append(d)
        continue
    for dpath, dlist, flist in os.walk(d):
        for f in flist:
            if f.endswith('.h'):
                path = dpath + '/' + f
                if not EXCLUDE_PATH_RE.match(path):
                    _list.append(dpath + '/' + f)

HDR_LIST = _list

INC = io.StringIO()
for h in HDR_LIST:
    l = '#include "{}"'.format(h)
    print(l, file=INC)
INC = INC.getvalue()

INCDIR_LIST = [
    "build",
    "build/lib/src",
    "build/ldms/src",
    "lib/src",
    "lib/src/zap",
    "ldms/src/core",
    "ldms/src/ldmsd",
]
INCDIR_LIST = [ "-I" + OVIS_DIR + "/" + d for d in INCDIR_LIST ]

def all_enum():
    cmd = ["/usr/bin/gcc", "-E"] + INCDIR_LIST + ["-"]
    proc = sp.Popen(cmd, stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE, shell=False)
    proc.stdin.write(INC.encode())
    proc.stdin.close()
    out = proc.stdout.readlines()
    err = proc.stderr.readlines()
    proc.wait()
    enum_begin = re.compile(r".*enum\s+\S*\s*\{")
    enum_end = re.compile(r".*\}")
    enum_tkn = re.compile(r"\s*(\w+)")
    in_enum = False
    enum_set = set()
    for l in out:
        l = l.strip().decode()
        if not l:
            continue
        if in_enum:
            if enum_end.match(l):
                in_enum = False
                v = sio.getvalue()
                for e in v.split(','):
                    m = enum_tkn.match(e)
                    if not m:
                        continue
                    enum_set.add(m.group(1))
            else:
                sio.write(l)
        else:
            if enum_begin.match(l):
                in_enum = True
                sio = io.StringIO()
    enum_lst = list(enum_set)
    enum_lst.sort()
    return enum_lst

if args.enum_list:
    enum_lst = list(set(l.strip() for l in open(args.enum_list)))
else:
    enum_lst = all_enum()
enum_lst.sort()

# prep prog in /tmp
fd, prog = tempfile.mkstemp(prefix="enumsym")
os.close(fd)
cmd = ["/usr/bin/gcc", "-x", "c", "-o", prog] + INCDIR_LIST + ["-"]
proc = sp.Popen(cmd, stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE, shell=False)
sio = io.StringIO()
sio.write('#include <stdio.h>\n')
sio.write(INC)
sio.write('\n')
sio.write('#define PRINT_ENUM(E) printf("%s=%d\\n", #E, E)\n')
sio.write('int main(int argc, char **argv)\n')
sio.write('{\n')
for e in enum_lst:
    sio.write(' PRINT_ENUM({});\n'.format(e))
sio.write('}\n')

out, err = proc.communicate(input = sio.getvalue().encode())
if proc.returncode:
    print(err.decode())
    os.unlink(prog)
    raise RuntimeError("Compilation error: {}".format(err.decode()))
out = sp.getoutput(prog)
os.unlink(prog)
print(out)

# Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2020 NTESS Corporation. All rights reserved.
# Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
# license for use of this work by or on behalf of the U.S. Government.
# Export of this program may require a license from the United States
# Government.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the BSD-type
# license below:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#      Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#      Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditions and the following
#      disclaimer in the documentation and/or other materials provided
#      with the distribution.
#
#      Neither the name of NTESS Corporation, Open Grid Computing nor
#      the names of any contributors may be used to endorse or promote
#      products derived from this software without specific prior
#      written permission.
#
#      Modified source versions must be plainly marked as such, and
#      must not be misrepresented as being the original software.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from __future__ import print_function
from cpython cimport PyObject, Py_INCREF, Py_DECREF, PyGILState_Ensure, \
                     PyGILState_Release, PyGILState_STATE, \
                     PyBytes_FromStringAndSize, \
                     Py_LT, Py_LE, Py_EQ, Py_NE, Py_GT,Py_GE

from libc.stdint cimport *
from libc.stdlib cimport calloc, malloc, free, realloc
import datetime as dt
import struct
import io
import os
import sys
import copy
import json
from queue import Queue
cimport cython
cimport ldms

__doc__ = """Lighweight Distibuted Metric Service (LDMS) for Python

This module provides LDMS to Python.

Synchronous Active-side Example
-------------------------------
```
from ovis_ldms import ldms
ldms.init(16*1024*1024)
x = ldms.Xprt()
# connect
x.connect(host="localhost", port=10000)
# dir
dlist = x.dir() # list of dir result
# Lookup
slist = [] # list of sets from remote lookup
for d in dlist:
    s = x.lookup(d.name)
    slist.append(s)
# Update
for s in slist:
    s.update()
```

Asynchronous Active-side Example
--------------------------------
```
from ovis_ldms import ldms
ldms.init(16*1024*1024)
x = ldms.Xprt()
slist = [] # list of sets from remote lookup

def update_cb(lset, flags, arg):
    print("{} update completed".format(lset.name))

def lookup_cb(xprt, status, more, lset, arg):
    global slist
    if lset:
        slist.append(lset)
        lset.update(update_cb, arg+10)

def dir_cb(xprt, status, dd, arg):
    if dd.type != ldms.DIR_LIST:
        return # ignore other types
    for sd in dd.set_data:
        xprt.lookup(sd.inst_name, cb=lookup_cb, cb_arg=arg+10)

def xprt_cb(xprt, ev, arg):
    if ev.type == ldms.EVENT_CONNECTED:
        xprt.dir(cb = dir_cb, cb_arg = arg+10)

x.connect(host="localhost", port=10002, cb=xprt_cb, cb_arg=10)
```

Set Data Access Examples
------------------------
```
# See Set class for details.
# supposed s is an ldms.Set.
print(s.name) # set name
print(len(s)) # number of metrics in the set
print(s["x"]) # access by metric name
print(s[1]) # access by metric ID (index)
print(s[-1]) # negative index also work (-1 == last element)
print(s[:]) # access by slice (all metrics in this case)
print(s[3][4]) # if metric 3 is an array, subindex also works
print(s.keys()) # mimic dict.keys()
print(s.values()) # mimic dict.values()
print(s.items()) # mimic dict.items()
for k, v in s.items():
    print("key:", k, "val:", v)
for k in s: # mimic dict behavior -- iterating through keys
    print("key:", k, "val:", s[k])
```

Synchronous Passive-side Example
--------------------------------
```
from ovis_ldms import ldms
ldms.init(16*1024*1024)
lx = ldms.Xprt()
lx.listen(port=10001)
new_x = lx.accept() # this will block
# All published sets will be available to the peers
```

Asynchronous Passive-side Example
---------------------------------
```
from ovis_ldms import ldms
ldms.init(16*1024*1024)

def listen_cb(x, ev, arg):
    # x is the new transport for CONNECTED event
    if ev.type == ldms.EVENT_CONNECTED:
        print("New xprt:", hex(id(x)))
    elif ev.type == ldms.EVENT_DISCONNECTED:
        print("Xprt:", hex(id(x)), "disconnected")

lx = ldms.Xprt()
lx.listen(port=10000, cb=listen_cb, cb_arg=None)
```

Set Creation Example
--------------------
```
from ovis_ldms import ldms
ldms.init(16*1024*1024)
# Create a schema comprising `x` and `y` float attributes
schema = ldms.Schema(name = "my_schema", metric_list = [
                                            ("x", "float"),
                                            ("y", "float"),
                                         ]) # see Schema class for more info.
# Create a set from the schema
s = ldms.Set(name="my_set", schema=schema)
# Make set available to peers
s.publish()
# Begin data transaction
s.transaction_begin()
# set metric data
s[0] = 1.0
s["y"] = 2.0
# or equivalently set by `slice`
s[:] = (1.0, 2.0)
# End data transaction
s.transaction_end()
```


"""

cdef extern void PyEval_InitThreads()

# Need initialization before PyGILState_Ensure(), cb, PyGILState_Release().
# 2nd call to this function is a no-op.
PyEval_InitThreads()

def init(int max_sz):
    """init(max_sz) - initialize LDMS with memory pool `max_sz` bytes"""
    cdef int rc = ldms_init(max_sz)
    if rc:
        raise OSError(rc, "ldms_init({}) error: {}" \
                          .format(max_sz, ERRNO_SYM(rc)))


# This is a logging function for LDMS library
cdef void xprt_log(const char *fmt, ...) nogil:
    cdef va_list va
    va_start(va, <void*>fmt)
    vfprintf(stderr, fmt, va)
    va_end(va)


# errno symbol mapping
cpdef dict ERRNO_SYM_TBL = {
        E2BIG           : "E2BIG({})".format(E2BIG),
        EACCES          : "EACCES({})".format(EACCES),
        EADDRINUSE      : "EADDRINUSE({})".format(EADDRINUSE),
        EADDRNOTAVAIL   : "EADDRNOTAVAIL({})".format(EADDRNOTAVAIL),
        EAFNOSUPPORT    : "EAFNOSUPPORT({})".format(EAFNOSUPPORT),
        EAGAIN          : "EAGAIN({})".format(EAGAIN),
        EALREADY        : "EALREADY({})".format(EALREADY),
        EBADE           : "EBADE({})".format(EBADE),
        EBADF           : "EBADF({})".format(EBADF),
        EBADFD          : "EBADFD({})".format(EBADFD),
        EBADMSG         : "EBADMSG({})".format(EBADMSG),
        EBADR           : "EBADR({})".format(EBADR),
        EBADRQC         : "EBADRQC({})".format(EBADRQC),
        EBADSLT         : "EBADSLT({})".format(EBADSLT),
        EBUSY           : "EBUSY({})".format(EBUSY),
        ECHILD          : "ECHILD({})".format(ECHILD),
        ECHRNG          : "ECHRNG({})".format(ECHRNG),
        ECOMM           : "ECOMM({})".format(ECOMM),
        ECONNABORTED    : "ECONNABORTED({})".format(ECONNABORTED),
        ECONNREFUSED    : "ECONNREFUSED({})".format(ECONNREFUSED),
        ECONNRESET      : "ECONNRESET({})".format(ECONNRESET),
        EDEADLK         : "EDEADLK({})".format(EDEADLK),
        EDEADLOCK       : "EDEADLOCK({})".format(EDEADLOCK),
        EDESTADDRREQ    : "EDESTADDRREQ({})".format(EDESTADDRREQ),
        EDOM            : "EDOM({})".format(EDOM),
        EDQUOT          : "EDQUOT({})".format(EDQUOT),
        EEXIST          : "EEXIST({})".format(EEXIST),
        EFAULT          : "EFAULT({})".format(EFAULT),
        EFBIG           : "EFBIG({})".format(EFBIG),
        EHOSTDOWN       : "EHOSTDOWN({})".format(EHOSTDOWN),
        EHOSTUNREACH    : "EHOSTUNREACH({})".format(EHOSTUNREACH),
        EIDRM           : "EIDRM({})".format(EIDRM),
        EILSEQ          : "EILSEQ({})".format(EILSEQ),
        EINPROGRESS     : "EINPROGRESS({})".format(EINPROGRESS),
        EINTR           : "EINTR({})".format(EINTR),
        EINVAL          : "EINVAL({})".format(EINVAL),
        EIO             : "EIO({})".format(EIO),
        EISCONN         : "EISCONN({})".format(EISCONN),
        EISDIR          : "EISDIR({})".format(EISDIR),
        EISNAM          : "EISNAM({})".format(EISNAM),
        ELOOP           : "ELOOP({})".format(ELOOP),
        EMFILE          : "EMFILE({})".format(EMFILE),
        EMLINK          : "EMLINK({})".format(EMLINK),
        EMSGSIZE        : "EMSGSIZE({})".format(EMSGSIZE),
        EMULTIHOP       : "EMULTIHOP({})".format(EMULTIHOP),
        ENAMETOOLONG    : "ENAMETOOLONG({})".format(ENAMETOOLONG),
        ENETDOWN        : "ENETDOWN({})".format(ENETDOWN),
        ENETRESET       : "ENETRESET({})".format(ENETRESET),
        ENETUNREACH     : "ENETUNREACH({})".format(ENETUNREACH),
        ENFILE          : "ENFILE({})".format(ENFILE),
        ENOBUFS         : "ENOBUFS({})".format(ENOBUFS),
        ENODATA         : "ENODATA({})".format(ENODATA),
        ENODEV          : "ENODEV({})".format(ENODEV),
        ENOENT          : "ENOENT({})".format(ENOENT),
        ENOEXEC         : "ENOEXEC({})".format(ENOEXEC),
        ENOLCK          : "ENOLCK({})".format(ENOLCK),
        ENOLINK         : "ENOLINK({})".format(ENOLINK),
        ENOMEM          : "ENOMEM({})".format(ENOMEM),
        ENOMSG          : "ENOMSG({})".format(ENOMSG),
        ENONET          : "ENONET({})".format(ENONET),
        ENOPKG          : "ENOPKG({})".format(ENOPKG),
        ENOPROTOOPT     : "ENOPROTOOPT({})".format(ENOPROTOOPT),
        ENOSPC          : "ENOSPC({})".format(ENOSPC),
        ENOSR           : "ENOSR({})".format(ENOSR),
        ENOSTR          : "ENOSTR({})".format(ENOSTR),
        ENOSYS          : "ENOSYS({})".format(ENOSYS),
        ENOTBLK         : "ENOTBLK({})".format(ENOTBLK),
        ENOTCONN        : "ENOTCONN({})".format(ENOTCONN),
        ENOTDIR         : "ENOTDIR({})".format(ENOTDIR),
        ENOTEMPTY       : "ENOTEMPTY({})".format(ENOTEMPTY),
        ENOTSOCK        : "ENOTSOCK({})".format(ENOTSOCK),
        ENOTSUP         : "ENOTSUP({})".format(ENOTSUP),
        ENOTTY          : "ENOTTY({})".format(ENOTTY),
        ENOTUNIQ        : "ENOTUNIQ({})".format(ENOTUNIQ),
        ENXIO           : "ENXIO({})".format(ENXIO),
        EOPNOTSUPP      : "EOPNOTSUPP({})".format(EOPNOTSUPP),
        EOVERFLOW       : "EOVERFLOW({})".format(EOVERFLOW),
        EPERM           : "EPERM({})".format(EPERM),
        EPFNOSUPPORT    : "EPFNOSUPPORT({})".format(EPFNOSUPPORT),
        EPIPE           : "EPIPE({})".format(EPIPE),
        EPROTO          : "EPROTO({})".format(EPROTO),
        EPROTONOSUPPORT : "EPROTONOSUPPORT({})".format(EPROTONOSUPPORT),
        EPROTOTYPE      : "EPROTOTYPE({})".format(EPROTOTYPE),
        ERANGE          : "ERANGE({})".format(ERANGE),
        EREMCHG         : "EREMCHG({})".format(EREMCHG),
        EREMOTE         : "EREMOTE({})".format(EREMOTE),
        EREMOTEIO       : "EREMOTEIO({})".format(EREMOTEIO),
        ERESTART        : "ERESTART({})".format(ERESTART),
        EROFS           : "EROFS({})".format(EROFS),
        ESHUTDOWN       : "ESHUTDOWN({})".format(ESHUTDOWN),
        ESPIPE          : "ESPIPE({})".format(ESPIPE),
        ESOCKTNOSUPPORT : "ESOCKTNOSUPPORT({})".format(ESOCKTNOSUPPORT),
        ESRCH           : "ESRCH({})".format(ESRCH),
        ESTALE          : "ESTALE({})".format(ESTALE),
        ESTRPIPE        : "ESTRPIPE({})".format(ESTRPIPE),
        ETIME           : "ETIME({})".format(ETIME),
        ETIMEDOUT       : "ETIMEDOUT({})".format(ETIMEDOUT),
        EUSERS          : "EUSERS({})".format(EUSERS),
        EWOULDBLOCK     : "EWOULDBLOCK({})".format(EWOULDBLOCK),
        EXFULL          : "EXFULL({})".format(EXFULL)
    }

cdef str ERRNO_SYM(int e):
    """Convert errno (`int`) to error symbol (e.g. 2 -> "ENOENT(2)")"""
    return ERRNO_SYM_TBL.get(e, "UNKNOWN_ERRNO_{}".format(e))


# ============================ #
# == metric getter wrappers == #
# ============================ #

# The functions wrap the associate C functions so that they become Python
# callables.

cdef py_ldms_metric_get_char(Set s, int m_idx):
    cdef char buf[2]
    buf[0] = ldms_metric_get_char(s.rbd, m_idx)
    buf[1] = 0 # terminate the string
    return STR(buf)

cdef py_ldms_metric_array_get_str(Set s, int m_idx):
    return STR(ldms_metric_array_get_str(s.rbd, m_idx))

cdef py_ldms_metric_get_u8(Set s, int m_idx):
    return ldms_metric_get_u8(s.rbd, m_idx)

cdef py_ldms_metric_array_get_u8(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_u8(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_s8(Set s, int m_idx):
    return ldms_metric_get_s8(s.rbd, m_idx)

cdef py_ldms_metric_array_get_s8(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_s8(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_u16(Set s, int m_idx):
    return ldms_metric_get_u16(s.rbd, m_idx)

cdef py_ldms_metric_array_get_u16(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_u16(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_s16(Set s, int m_idx):
    return ldms_metric_get_s16(s.rbd, m_idx)

cdef py_ldms_metric_array_get_s16(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_s16(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_u32(Set s, int m_idx):
    return ldms_metric_get_u32(s.rbd, m_idx)

cdef py_ldms_metric_array_get_u32(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_u32(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_s32(Set s, int m_idx):
    return ldms_metric_get_s32(s.rbd, m_idx)

cdef py_ldms_metric_array_get_s32(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_s32(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_u64(Set s, int m_idx):
    return ldms_metric_get_u64(s.rbd, m_idx)

cdef py_ldms_metric_array_get_u64(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_u64(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_s64(Set s, int m_idx):
    return ldms_metric_get_s64(s.rbd, m_idx)

cdef py_ldms_metric_array_get_s64(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_s64(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_float(Set s, int m_idx):
    return ldms_metric_get_float(s.rbd, m_idx)

cdef py_ldms_metric_array_get_float(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_float(s.rbd, m_idx, e_idx)

cdef py_ldms_metric_get_double(Set s, int m_idx):
    return ldms_metric_get_double(s.rbd, m_idx)

cdef py_ldms_metric_array_get_double(Set s, int m_idx, int e_idx):
    return ldms_metric_array_get_double(s.rbd, m_idx, e_idx)

METRIC_GETTER_TBL = {
        LDMS_V_CHAR : py_ldms_metric_get_char,
        LDMS_V_U8   : py_ldms_metric_get_u8,
        LDMS_V_S8   : py_ldms_metric_get_s8,
        LDMS_V_U16  : py_ldms_metric_get_u16,
        LDMS_V_S16  : py_ldms_metric_get_s16,
        LDMS_V_U32  : py_ldms_metric_get_u32,
        LDMS_V_S32  : py_ldms_metric_get_s32,
        LDMS_V_U64  : py_ldms_metric_get_u64,
        LDMS_V_S64  : py_ldms_metric_get_s64,
        LDMS_V_F32  : py_ldms_metric_get_float,
        LDMS_V_D64  : py_ldms_metric_get_double,

        LDMS_V_CHAR_ARRAY : py_ldms_metric_array_get_str,

        LDMS_V_U8_ARRAY   : py_ldms_metric_array_get_u8,
        LDMS_V_S8_ARRAY   : py_ldms_metric_array_get_s8,
        LDMS_V_U16_ARRAY  : py_ldms_metric_array_get_u16,
        LDMS_V_S16_ARRAY  : py_ldms_metric_array_get_s16,
        LDMS_V_U32_ARRAY  : py_ldms_metric_array_get_u32,
        LDMS_V_S32_ARRAY  : py_ldms_metric_array_get_s32,
        LDMS_V_U64_ARRAY  : py_ldms_metric_array_get_u64,
        LDMS_V_S64_ARRAY  : py_ldms_metric_array_get_s64,
        LDMS_V_F32_ARRAY  : py_ldms_metric_array_get_float,
        LDMS_V_D64_ARRAY  : py_ldms_metric_array_get_double,
    }


# ============================ #
# == metric setter wrappers == #
# ============================ #

# The functions wrap the associate C functions so that they become Python
# callables.

cdef py_ldms_metric_set_char(Set s, int m_idx, val):
    if type(val) not in (str, bytes) or len(val) != 1:
        raise TypeError("A char must be a `str` or `bytes` of length 1")
    return ldms_metric_set_char(s.rbd, m_idx, BYTES(val)[0])

cdef py_ldms_metric_array_set_str(Set s, int m_idx, val):
    return ldms_metric_array_set_str(s.rbd, m_idx, BYTES(val))

cdef py_ldms_metric_set_u8(Set s, int m_idx, val):
    return ldms_metric_set_u8(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_u8(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_u8(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_s8(Set s, int m_idx, val):
    return ldms_metric_set_s8(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_s8(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_s8(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_u16(Set s, int m_idx, val):
    return ldms_metric_set_u16(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_u16(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_u16(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_s16(Set s, int m_idx, val):
    return ldms_metric_set_s16(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_s16(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_s16(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_u32(Set s, int m_idx, val):
    return ldms_metric_set_u32(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_u32(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_u32(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_s32(Set s, int m_idx, val):
    return ldms_metric_set_s32(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_s32(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_s32(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_u64(Set s, int m_idx, val):
    return ldms_metric_set_u64(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_u64(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_u64(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_s64(Set s, int m_idx, val):
    return ldms_metric_set_s64(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_s64(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_s64(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_float(Set s, int m_idx, val):
    return ldms_metric_set_float(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_float(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_float(s.rbd, m_idx, e_idx, val)

cdef py_ldms_metric_set_double(Set s, int m_idx, val):
    return ldms_metric_set_double(s.rbd, m_idx, val)

cdef py_ldms_metric_array_set_double(Set s, int m_idx, int e_idx, val):
    return ldms_metric_array_set_double(s.rbd, m_idx, e_idx, val)

METRIC_SETTER_TBL = {
        LDMS_V_CHAR : py_ldms_metric_set_char,
        LDMS_V_U8   : py_ldms_metric_set_u8,
        LDMS_V_S8   : py_ldms_metric_set_s8,
        LDMS_V_U16  : py_ldms_metric_set_u16,
        LDMS_V_S16  : py_ldms_metric_set_s16,
        LDMS_V_U32  : py_ldms_metric_set_u32,
        LDMS_V_S32  : py_ldms_metric_set_s32,
        LDMS_V_U64  : py_ldms_metric_set_u64,
        LDMS_V_S64  : py_ldms_metric_set_s64,
        LDMS_V_F32  : py_ldms_metric_set_float,
        LDMS_V_D64  : py_ldms_metric_set_double,

        LDMS_V_CHAR_ARRAY : py_ldms_metric_array_set_str,

        LDMS_V_U8_ARRAY   : py_ldms_metric_array_set_u8,
        LDMS_V_S8_ARRAY   : py_ldms_metric_array_set_s8,
        LDMS_V_U16_ARRAY  : py_ldms_metric_array_set_u16,
        LDMS_V_S16_ARRAY  : py_ldms_metric_array_set_s16,
        LDMS_V_U32_ARRAY  : py_ldms_metric_array_set_u32,
        LDMS_V_S32_ARRAY  : py_ldms_metric_array_set_s32,
        LDMS_V_U64_ARRAY  : py_ldms_metric_array_set_u64,
        LDMS_V_S64_ARRAY  : py_ldms_metric_array_set_s64,
        LDMS_V_F32_ARRAY  : py_ldms_metric_array_set_float,
        LDMS_V_D64_ARRAY  : py_ldms_metric_array_set_double,
    }

#####################################
### value type conversion utility ###
#####################################
LDMS_VALUE_TYPE_TBL = {
        str      : LDMS_V_CHAR_ARRAY,
        int      : LDMS_V_S64,
        float    : LDMS_V_F32,

        "char"   : LDMS_V_CHAR,
        "s8"     : LDMS_V_S8,
        "int8"   : LDMS_V_S8,
        "u8"     : LDMS_V_U8,
        "uint8"  : LDMS_V_U8,
        "s16"    : LDMS_V_S16,
        "int16"  : LDMS_V_S16,
        "u16"    : LDMS_V_U16,
        "uint16" : LDMS_V_U16,
        "s32"    : LDMS_V_S32,
        "int32"  : LDMS_V_S32,
        "u32"    : LDMS_V_U32,
        "uint32" : LDMS_V_U32,
        "s64"    : LDMS_V_S64,
        "int64"  : LDMS_V_S64,
        "u64"    : LDMS_V_U64,
        "uint64" : LDMS_V_U64,
        "float"  : LDMS_V_F32,
        "f"      : LDMS_V_F32,
        "f32"    : LDMS_V_F32,
        "double" : LDMS_V_D64,
        "d"      : LDMS_V_D64,
        "d64"    : LDMS_V_D64,

        "str"      : LDMS_V_CHAR_ARRAY,
        "char[]"   : LDMS_V_CHAR_ARRAY,
        "s8[]"     : LDMS_V_S8_ARRAY,
        "int8[]"   : LDMS_V_S8_ARRAY,
        "u8[]"     : LDMS_V_U8_ARRAY,
        "uint8[]"  : LDMS_V_U8_ARRAY,
        "s16[]"    : LDMS_V_S16_ARRAY,
        "int16[]"  : LDMS_V_S16_ARRAY,
        "u16[]"    : LDMS_V_U16_ARRAY,
        "uint16[]" : LDMS_V_U16_ARRAY,
        "s32[]"    : LDMS_V_S32_ARRAY,
        "int32[]"  : LDMS_V_S32_ARRAY,
        "u32[]"    : LDMS_V_U32_ARRAY,
        "uint32[]" : LDMS_V_U32_ARRAY,
        "s64[]"    : LDMS_V_S64_ARRAY,
        "int64[]"  : LDMS_V_S64_ARRAY,
        "u64[]"    : LDMS_V_U64_ARRAY,
        "uint64[]" : LDMS_V_U64_ARRAY,
        "float[]"  : LDMS_V_F32_ARRAY,
        "f[]"      : LDMS_V_F32_ARRAY,
        "f32[]"    : LDMS_V_F32_ARRAY,
        "double[]" : LDMS_V_D64_ARRAY,
        "d[]"      : LDMS_V_D64_ARRAY,
        "d64[]"    : LDMS_V_D64_ARRAY,

        "char_array"   : LDMS_V_CHAR_ARRAY,
        "s8_array"     : LDMS_V_S8_ARRAY,
        "int8_array"   : LDMS_V_S8_ARRAY,
        "u8_array"     : LDMS_V_U8_ARRAY,
        "uint8_array"  : LDMS_V_U8_ARRAY,
        "s16_array"    : LDMS_V_S16_ARRAY,
        "int16_array"  : LDMS_V_S16_ARRAY,
        "u16_array"    : LDMS_V_U16_ARRAY,
        "uint16_array" : LDMS_V_U16_ARRAY,
        "s32_array"    : LDMS_V_S32_ARRAY,
        "int32_array"  : LDMS_V_S32_ARRAY,
        "u32_array"    : LDMS_V_U32_ARRAY,
        "uint32_array" : LDMS_V_U32_ARRAY,
        "s64_array"    : LDMS_V_S64_ARRAY,
        "int64_array"  : LDMS_V_S64_ARRAY,
        "u64_array"    : LDMS_V_U64_ARRAY,
        "uint64_array" : LDMS_V_U64_ARRAY,
        "float_array"  : LDMS_V_F32_ARRAY,
        "f_array"      : LDMS_V_F32_ARRAY,
        "f32_array"    : LDMS_V_F32_ARRAY,
        "double_array" : LDMS_V_D64_ARRAY,
        "d_array"      : LDMS_V_D64_ARRAY,
        "d64_array"    : LDMS_V_D64_ARRAY,

        LDMS_V_CHAR : LDMS_V_CHAR,
        LDMS_V_S8   : LDMS_V_S8,
        LDMS_V_S8   : LDMS_V_S8,
        LDMS_V_U8   : LDMS_V_U8,
        LDMS_V_U8   : LDMS_V_U8,
        LDMS_V_S16  : LDMS_V_S16,
        LDMS_V_S16  : LDMS_V_S16,
        LDMS_V_U16  : LDMS_V_U16,
        LDMS_V_U16  : LDMS_V_U16,
        LDMS_V_S32  : LDMS_V_S32,
        LDMS_V_S32  : LDMS_V_S32,
        LDMS_V_U32  : LDMS_V_U32,
        LDMS_V_U32  : LDMS_V_U32,
        LDMS_V_S64  : LDMS_V_S64,
        LDMS_V_S64  : LDMS_V_S64,
        LDMS_V_U64  : LDMS_V_U64,
        LDMS_V_U64  : LDMS_V_U64,
        LDMS_V_F32  : LDMS_V_F32,
        LDMS_V_F32  : LDMS_V_F32,
        LDMS_V_F32  : LDMS_V_F32,
        LDMS_V_D64  : LDMS_V_D64,
        LDMS_V_D64  : LDMS_V_D64,
        LDMS_V_D64  : LDMS_V_D64,

        LDMS_V_CHAR_ARRAY : LDMS_V_CHAR_ARRAY,
        LDMS_V_S8_ARRAY   : LDMS_V_S8_ARRAY,
        LDMS_V_S8_ARRAY   : LDMS_V_S8_ARRAY,
        LDMS_V_U8_ARRAY   : LDMS_V_U8_ARRAY,
        LDMS_V_U8_ARRAY   : LDMS_V_U8_ARRAY,
        LDMS_V_S16_ARRAY  : LDMS_V_S16_ARRAY,
        LDMS_V_S16_ARRAY  : LDMS_V_S16_ARRAY,
        LDMS_V_U16_ARRAY  : LDMS_V_U16_ARRAY,
        LDMS_V_U16_ARRAY  : LDMS_V_U16_ARRAY,
        LDMS_V_S32_ARRAY  : LDMS_V_S32_ARRAY,
        LDMS_V_S32_ARRAY  : LDMS_V_S32_ARRAY,
        LDMS_V_U32_ARRAY  : LDMS_V_U32_ARRAY,
        LDMS_V_U32_ARRAY  : LDMS_V_U32_ARRAY,
        LDMS_V_S64_ARRAY  : LDMS_V_S64_ARRAY,
        LDMS_V_S64_ARRAY  : LDMS_V_S64_ARRAY,
        LDMS_V_U64_ARRAY  : LDMS_V_U64_ARRAY,
        LDMS_V_U64_ARRAY  : LDMS_V_U64_ARRAY,
        LDMS_V_F32_ARRAY  : LDMS_V_F32_ARRAY,
        LDMS_V_F32_ARRAY  : LDMS_V_F32_ARRAY,
        LDMS_V_F32_ARRAY  : LDMS_V_F32_ARRAY,
        LDMS_V_D64_ARRAY  : LDMS_V_D64_ARRAY,
        LDMS_V_D64_ARRAY  : LDMS_V_D64_ARRAY,
        LDMS_V_D64_ARRAY  : LDMS_V_D64_ARRAY,
    }

cdef ldms_value_type LDMS_VALUE_TYPE(t):
    return LDMS_VALUE_TYPE_TBL[t]


cdef class Ptr(object):
    """Pointer wrapper so that C pointer can be passed around as PyOBJ"""
    cdef void *c_ptr

cdef Ptr PTR(void *ptr):
    """Returns a `Ptr` object wrapping the given `ptr` pointer"""
    po = Ptr()
    po.c_ptr = ptr
    return po

cdef bytes BYTES(o):
    """Convert Python object `s` to `bytes` (c-string compatible)"""
    # a wrapper to solve the annoying bytes vs str in python3
    if type(o) == bytes:
        return o
    return str(o).encode()

cdef str STR(o):
    """Convert to `str`, None stays None"""
    # a wrapper to solve the annoying bytes vs str in python3
    if o is None:
        return None
    if type(o) == bytes:
        return o.decode()
    return str(o)


cdef class XprtEvent(object):
    """An LDMS transport event

    This object is sent to the application in the callback function supplied to
    `Xprt.connect()` or `Xprt.listen()`. The object has two attributes:

    - `type`: an ldms_xprt_event_type enum, one of the
              - EVENT_CONNECTED
              - EVENT_REJECTED
              - EVENT_ERROR
              - EVENT_DISCONNECTED
              - EVENT_RECV
              - EVENT_SEND_COMPLETE
    - `data`: a byte array containing event data
    """

    cdef readonly object type
    """The ldms_xprt_event_type enumeration"""

    cdef readonly bytes data
    """A `bytes` containing event data"""

    # NOTE: This is a Python object wrapper of `struct ldms_xprt_event`.
    #
    #       The values of the attributes are also new Python objects. So, we
    #       don't have to worry about the actual `ldms_xprt_event_t` being
    #       destroyed in the C level after the callback.
    def __cinit__(self, Ptr ptr):
        cdef ldms_xprt_event_t e = <ldms_xprt_event_t>ptr.c_ptr
        self.type = ldms_xprt_event_type(e.type)
        self.data = e.data[:e.data_len]


# This is the C callback function for active xprt (the one initiating connect).
# It calls the Python callback callable if provided. Otherwise, it works
# internally to provide blocking `Xprt.connect()` and `Xprt.recv()`.
cdef void xprt_cb(ldms_t _x, ldms_xprt_event *e, void *arg) with gil:
    cdef Xprt x = <Xprt>arg
    cdef bytes b
    if x._conn_cb:
        # Call the callback
        x._conn_cb(x, XprtEvent(PTR(e)), x._conn_cb_arg)
        return
    # Else, No callback, this must be blocking connect. Post the semaphore to
    # unblock the connect-calling thread.
    if e.type == EVENT_CONNECTED:
        x._conn_rc = 0
        x._conn_rc_msg = "CONNECTED"
        Py_INCREF(x)
    elif e.type == EVENT_REJECTED:
        x._conn_rc = ECONNREFUSED
        x._conn_rc_msg = "REJECTED"
    elif e.type == EVENT_ERROR:
        x._conn_rc = ECONNABORTED
        x._conn_rc_msg = "ERROR"
    elif e.type == EVENT_DISCONNECTED:
        x._conn_rc = ENOTCONN
        x._conn_rc_msg = "DISCONNECTED"
    elif e.type == EVENT_RECV:
        b = PyBytes_FromStringAndSize(e.data, e.data_len)
        x._recv_queue.put(b)
        # do NOT sem_post()
        return
    elif e.type == EVENT_SEND_COMPLETE:
        # do NOT sem_post()
        return
    else:
        raise OSError(EINVAL, "Unknown LDMS event type {}".format(e.type))
    sem_post(&x._conn_sem)
    if e.type == EVENT_DISCONNECTED:
        Py_DECREF(x) # taken when CONNECTED


# This is the C callback function for passive transports (the listening
# transport and all transports accepted by it). It calls the Python callback
# callable if provided. Otherwise, it works internally to provide blocking
# `Xprt.accept()` and `Xprt.recv()`.
cdef void passive_xprt_cb(ldms_t _x, ldms_xprt_event *e, void *arg) with gil:
    cdef Xprt lx = <Xprt>arg # arg is the listening Xprt
    cdef Xprt x = lx._psv_xprts.get(<uint64_t>_x)
    if not x:
        x = Xprt(xprt_ptr=PTR(_x))
        x._conn_cb = lx._conn_cb
        x._conn_cb_arg = lx._conn_cb_arg
        lx._psv_xprts[<uint64_t>_x] = x
    if e.type == EVENT_DISCONNECTED or \
       e.type == EVENT_REJECTED or \
       e.type == EVENT_ERROR:
        lx._psv_xprts.pop(<uint64_t>_x, None)
    if x._conn_cb:
        # Call the callback
        x._conn_cb(x, XprtEvent(PTR(e)), x._conn_cb_arg)
        return
    # No callback
    if e.type == EVENT_CONNECTED:
        lx._accept_queue.put(x)
    elif e.type == EVENT_RECV:
        b = PyBytes_FromStringAndSize(e.data, e.data_len)
        x._recv_queue.put(b)
    # Else, ignore


cdef class DirSet(object):
    """An LDMS dir result containing set description"""
    # A wrapper of `struct ldms_dir_set_s`
    # NOTE: All attributes are valid Python objects. So, we don't have to worry
    #       about C-level structure being destroyed.

    cdef readonly str name
    """(str) The set instance name"""

    cdef readonly str inst_name
    """(str) The set instance name"""

    cdef readonly str schema_name
    """(str) The name of the schema"""

    cdef readonly str flags
    """(str) LDMS Flags"""

    cdef readonly size_t meta_size
    """(int) The size of the metadata part of the set"""

    cdef readonly size_t data_size
    """(int) The size of the data part of the set"""

    cdef readonly uid_t uid
    """(int) The UID of the set owner"""

    cdef readonly gid_t gid
    """(int) The GID of the set owner"""

    cdef readonly str perm
    """(str) The permission of the set"""

    cdef readonly int card
    """(int) The number of metrics in the set (cardinality)"""

    cdef readonly int array_card
    """(int) The length of set array"""

    cdef readonly long meta_gn
    """(int) The generation of the metadata"""

    cdef readonly long data_gn
    """(int) The generation number of the data"""

    cdef readonly tuple timestamp
    """(sec, usec) The latest transaction-end timestamp"""

    cdef readonly tuple duration
    """(sec, usec) The most-recent amount of time used in set transaction"""

    cdef readonly dict info
    """(dict) Additional set information supplied by the provider"""

    def __cinit__(self, Ptr ptr):
        cdef ldms_dir_set_s *ds = <ldms_dir_set_s*>ptr.c_ptr
        self.name = STR(ds.inst_name)
        self.inst_name = STR(ds.inst_name)
        self.schema_name = STR(ds.schema_name)
        self.flags = STR(ds.flags)
        self.meta_size = ds.meta_size
        self.data_size = ds.data_size
        self.uid = ds.uid
        self.gid = ds.gid
        self.perm = STR(ds.perm)
        self.card = ds.card
        self.array_card = ds.array_card
        self.meta_gn = ds.meta_gn
        self.data_gn = ds.meta_gn
        self.timestamp = (ds.timestamp.sec, ds.timestamp.usec)
        self.duration = (ds.duration.sec, ds.timestamp.usec)
        self.info = { ds.info[i].key : ds.info[i].value \
                      for i in range(0, ds.info_count) }


cdef class DirData(object):
    """The data for the `Xprt.dir()` callback"""

    cdef readonly object type
    """The DIR callback event type (DIR_LIST, DIR_UPD, DIR_ADD or DIR_DEL)"""

    cdef readonly int more
    """(int) 1 if there is more data for the event, otherwise 0"""

    cdef readonly list set_data
    """(list of DirSet) A list containing DirSet which describes the sets
       associated with the dir event"""

    # A wrapper of `struct ldms_dir_s`
    # NOTE: The `set_data` is copied out. We don't have to worry about the data
    #       being accessed after the callback.
    def __cinit__(self, Ptr ptr):
        cdef ldms_dir_s *d = <ldms_dir_s*>ptr.c_ptr
        self.type = d.type
        self.more = d.more
        self.set_data = [ DirSet(PTR(&d.set_data[i])) \
                                for i in range(0, d.set_count) ]


# This is the dir callback interposer. If Python dir callback is supplied to
# `Xprt.dir()`, this function calls the callback. Otherwise, it works internally
# to provide blocking `Xprt.dir()` results.
cdef void dir_cb(ldms_t _x, int status, ldms_dir_t d, void *arg) with gil:
    x = <Xprt>arg
    if x._dir_cb:
        # Call the callback
        x._dir_cb(x, status, DirData(PTR(d)), x._dir_cb_arg)
        ldms_xprt_dir_free(x.xprt, d)
        return
    # Else, use blocking-dir
    if status:
        x._dir_rc = status
        sem_post(&x._conn_sem)
    else:
        if d.type != LDMS_DIR_LIST:
            # NOTE warn about unhandling dir msg?
            return
        for i in range(0, d.set_count):
            x._dir_list.append(DirSet(PTR(&d.set_data[i])))
        if not d.more:
            x._dir_rc = 0
            sem_post(&x._conn_sem)
    ldms_xprt_dir_free(x.xprt, d)


# This is the lookup callback interposer. If Python lookup callback is provided
# to `Xprt.lookup()`, this function callas the Python callback. Otherwise, it
# works internally to provide blocking `Xprt.lookup()` results.
cdef void lookup_cb(ldms_t _x, ldms_lookup_status status, int more,
                    ldms_set_t s, void *arg) with gil:
    (px, cb, cb_arg, slist) = <tuple>arg
    x = <Xprt>px
    lset = Set(None, None, set_ptr=PTR(s)) if s else None
    if cb:
        # Call the callback
        cb(x, status, more, lset, cb_arg)
        if not more:
            Py_DECREF(<tuple>arg)
        return
    if status:
        x._lookup_rc = status
    if lset:
        slist.append(lset)
    if not more:
        sem_post(&x._lookup_sem)


# This is the update callback interposer. If Python update callback is provided
# to `Set.update()`, this function callas the Python callback. Otherwise, it
# works internally to provide blocking `Set.update()` results.
cdef void update_cb(ldms_t _t, ldms_set_t _s, int flags, void *arg) with gil:
    cdef int rc = LDMS_UPD_ERROR(flags)
    (ps, cb, cb_arg) = <tuple>arg
    s = <Set>ps
    if cb:
        cb(s, flags, cb_arg)
        if 0 == (flags & LDMS_UPD_F_MORE):
            Py_DECREF(<tuple>arg)
        return
    s._update_rc = rc
    if 0 == (flags & LDMS_UPD_F_MORE):
        sem_post(&s._sem)


cdef class Schema(object):
    """LDMS Set Schema for creating LDMS set

    The constructor accepts the following attributes:
    - name (str): the name of the schema,
    - array_card (int, default: 1): the length of the set array,
    - metric_list (list): the list of metric definitions,

    A metric definition could be:
    - a list or tuple of (NAME, TYPE [, LEN[]]). The NAME and
      TYPE are `str` and are mandatory. The LEN, UNITS, and META are optional,
      but specifying latter attribute requires former attribute. For example,
      LEN is needed before specifying UNITS. See more about TYPE below.
    - a dictionary specifying "name", "type", "length", and/or "meta"
      attributes. The "name" and "type" are mandatory. The rest of the
      attributes are optional.

    The following are valid values for metric TYPE:
    - for single character: "char", ldms.V_CHAR
    - for signed single byte: "s8", "int8", ldms.V_S8
    - for unsigned single byte: "u8", "uint8", ldms.V_U8
    - for signed single 16-bit int: "s16", "int16", ldms.V_S16
    - for unsigned single 16-bit int: "u16", "uint16", ldms.V_U16
    - for signed single 32-bit int: "s32", "int32", ldms.V_S32
    - for unsigned single 32-bit int: "u32", "uint32", ldms.V_U32
    - for signed single 64-bit int: "s64", "int64", ldms.V_S64
    - for unsigned singlt 64-bit int: "u64", "uint64", ldms.V_U64
    - for single 32-bit floating point: "float", "f32", float, ldms.V_F32
    - for single 64-bit floating point: "double", "d64", ldms.V_D64
    - for string (array of characters): "str", "char[]", "char_array", str,
                                        ldms.V_CHAR_ARRAY
    - for array of signed bytes: "s8[]", "int8[]", "s8_array", "int8_array",
                                 ldms.V_S8_ARRAY
    - for array of unsigned bytes: "u8[]", "uint8[]", "u8_array", "uint8_array",
                                   ldms.V_U8_ARRAY
    - for array of signed 16-bit int: "s16[]", "int16[]", "s16_array",
                                      "int16_array", ldms.V_S16_ARRAY
    - for array of unsigned 16-bit int: "u16[]", "uint16[]", "u16_array",
                                        "uint16_array", ldms.V_U16_ARRAY
    - for array of signed 32-bit int: "s32[]", "int32[]", "s32_array",
                                      "int32_array", ldms.V_S32_ARRAY
    - for array of unsigned 32-bit int: "u32[]", "uint32[]", "u32_array",
                                        "uint32_array", ldms.V_U32_ARRAY
    - for array of signed 64-bit int: "s64[]", "int64[]", "s64_array",
                                      "int64_array", ldms.V_S64_ARRAY
    - for array of unsigned 64-bit int: "u64[]", "uint64[]", "u64_array",
                                        "uint64_array", ldms.V_U64_ARRAY
    - for array of 32-bit floating point: "f32[]", "f[]", "float[]",
                                          "f32_array", "f_array", "float_array",
                                          ldms.V_F32_ARRAY
    - for array of 64-bit floating point: "d64[]", "d[]", "double[]",
                                          "d64_array", "d_array",
                                          "double_array", ldms.V_D64_ARRAY

    Example:
    >>> my_schema = ldms.Schema("everything", array_card = 60, metric_list = [
    ...    ( "a_char"    , "char"   ),
    ...
    ...    # using dict "length" is safely ignored in this case.
    ...    { "name": "an_uint8", "type": "uint8" },
    ...
    ...    ( "an_int16"  , "int16"  ),
    ...    ( "an_uint16" , "uint16" ),
    ...    ( "an_int32"  , "int32"  ),
    ...    ( "an_uint32" , "uint32" ),
    ...    ( "an_int64"  , int      ), # `int` is equivalent to "int64"
    ...    ( "an_uint64" , "uint64" ),
    ...    ( "a_float"   , "float"  ),
    ...    ( "a_double"  , "double" ),
    ...    ( "a_str"           , str                   , 5 ), # "char[]"
    ...    ( "an_int8_array"   , "int8[]"              , 5 ),
    ...    ( "an_uint8_array"  , "uint8_array"         , 5 ), # "_array" works too
    ...    ( "an_int16_array"  , ldms.V_S16_ARRAY      ,  5 ), # enum
    ...    ( "an_uint16_array" , "uint16[]"            , 5 ),
    ...    ( "an_int32_array"  , "int32[]"             , 5 ),
    ...    ( "an_uint32_array" , "uint32[]"            , 5 ),
    ...    ( "an_int64_array"  , "int64[]"             , 5 ),
    ...    ( "an_uint64_array" , "uint64[]"            , 5 ),
    ...    ( "a_float_array"   , "float[]"             , 5 , "SEC" ),
    ...    ( "a_double_array"  , "double[]"            , 5 , "uSEC" ),
    ... ]
    """
    cdef ldms_schema_t _schema

    def __init__(self, name, array_card=1, metric_list = list()):
        """S.__init__(name, array_card=1, metric_list=list())"""
        self._schema = ldms_schema_new(BYTES(name))
        if not self._schema:
            raise OSError(errno, "ldms_schema_new() error: {}" \
                                 .format(ERRNO_SYM(errno)))
        if metric_list:
            self.add_metrics(metric_list)

    def set_array_card(self, array_card):
        """S.set_array_card(num) - change the set array cardinality"""
        cdef rc = ldms_schema_array_card_set(self._schema, array_card)
        if rc:
            raise OSError(rc, "ldms_schema_array_card_set() error: {}" \
                              .format(ERRNO_SYM(rc)))

    def add_metric(self, name, metric_type, count=1, meta=False):
        """S.add_metric(name, type, count=1, meta=False)

        Add a metric to the schema"""
        cdef int idx
        if type(metric_type) == str:
            metric_type = metric_type.lower()
        t = LDMS_VALUE_TYPE(metric_type)
        if ldms_type_is_array(t):
            if meta:
                idx = ldms_schema_meta_array_add(self._schema, BYTES(name), t,
                                                 count)
            else:
                idx = ldms_schema_metric_array_add(self._schema, BYTES(name), t,
                                                   count)
        else:
            if meta:
                idx = ldms_schema_meta_add(self._schema, BYTES(name), t)
            else:
                idx = ldms_schema_metric_add(self._schema, BYTES(name), t)
        if idx < 0:
            # error = -idx
            raise OSError(-idx, "Adding metric to schema failed: {}" \
                                .format(ERRNO_SYM(-idx)))

    @cython.binding(True)
    def add_metrics(self, list mlist):
        """S.add_metrics(mlist) - add metrics in `mlist` to the schema

        See the description of Schema class for the definition of metrics in the
        `mlist`.
        """
        for o in mlist:
            if type(o) in (list, tuple):
                self.add_metric(*o)
            elif type(o) == dict:
                self.add_metric(
                            name = o["name"],
                            metric_type = o["type"],
                            count = o.get("count", 1),
                            meta = o.get("meta", False),
                        )


cdef class MetricArray(list):
    """A list-like object for metric array access

    The application does not usually create this object directly, but rather
    obtain MetricArray object by getting a metric of array type from an LDMS
    set.

    Usage examples:
    >>> a = _my_set[12] # assuming that _my_set[12] is u8 array of length 5
    >>> a[1] # get value of element index 1
    >>> a[2] = 10 # set value of element index 2 to 10
    >>> len(a) # get the length of the metric array
    >>> for e in a: # iterate through all elements in the array
    ...   print(e)
    >>> a[-1] # negative index works too, a[-1] is a[4]
    >>> a[2:4] # using 2:4 `slice` returns list() of values of index 2,3.
    >>> a == [7,8,9,10,11] # comparing to the list works too
    """
    cdef Set _set
    cdef ldms_set_t _rbd
    cdef ldms_value_type _type
    cdef int _mid
    cdef int _len
    cdef object _getter
    cdef object _setter

    def __init__(self, Set lset, int metric_id):
        self._set = lset
        self._rbd = lset.rbd
        self._mid = metric_id
        self._type = ldms_metric_type_get(self._rbd, self._mid)
        if not ldms_type_is_array(self._type):
            raise TypeError("set {}[{}] is not an array"\
                            .format(lset.name, metric_id))
        if self._type == LDMS_V_CHAR_ARRAY:
            raise TypeError("CHAR_ARRAY should be access as `str`")
        self._len = ldms_metric_array_get_len(self._rbd, self._mid)
        self._getter = METRIC_GETTER_TBL[self._type]
        self._setter = METRIC_SETTER_TBL[self._type]

    def __len__(self):
        return self._len

    def __iter__(self):
        for i in range(0, len(self)):
            yield self[i]

    def __reversed__(self):
        _len = len(self)
        for i in range(0, _len):
            yield self[_len - i - 1]

    def _cmp(self, other):
        for v0, v1 in zip(self, other):
            if v0 < v1:
                return -1
            if v0 > v1:
                return 1
        l0 = len(self)
        l1 = len(other)
        if l0 < l1:
            return -1
        if l0 > l1:
            return 1
        return 0

    def __richcmp__(self, other, int op):
        if op == Py_EQ:
            return self._cmp(other) == 0
        elif op == Py_NE:
            return self._cmp(other) != 0
        elif op == Py_LE:
            return self._cmp(other) <= 0
        elif op == Py_LT:
            return self._cmp(other) < 0
        elif op == Py_GE:
            return self._cmp(other) >= 0
        elif op == Py_GT:
            return self._cmp(other) > 0

    def __getitem__(self, idx):
        if type(idx) == slice:
            return [ self._getter(self._set, self._mid, i) \
                        for i in range(*idx.indices(self._len)) ]
        if idx < 0:
            idx += self._len
        return self._getter(self._set, self._mid, idx)

    def __setitem__(self, idx, val):
        if type(idx) == slice:
            for i,v in zip(range(*idx.indices(self._len)), val):
                self._setter(self._set, self._mid, i, v)
        else:
            if idx < 0:
                idx += self._len
            self._setter(self._set, self._mid, idx, val)

    def __delitem__(self, key):
        raise TypeError("MetricArray does not support item deletion")

    def __repr__(self):
        sio = io.StringIO()
        print("[", ", ".join(str(s) for s in self), "]", file=sio, end="", sep="")
        return sio.getvalue()

    def __call__(self, *args, **kwargs):
        return self # mimic py_ldms_metric_get_* functions

    def __iadd__(self, other):
        raise TypeError("MetricArray does not support element appending")

    def append(self, element):
        raise TypeError("MetricArray does not support element appending")

    def __imul__(self, val):
        raise TypeError("MetricArray does not support `*=` operation")

    def __mul__(self, val):
        raise TypeError("MetricArray does not support `*` operation")

    def __rmul__(self, val):
        raise TypeError("MetricArray does not support `*` operation")

    def clear(self):
        raise TypeError("MetricArray does not support `clear()`")

    def copy(self):
        raise TypeError("MetricArray does not support `copy()`")

    def count(self, *args):
        raise TypeError("MetricArray does not support `count()`")

    def extend(self, *args):
        raise TypeError("MetricArray does not support `extend()`")

    def index(self, *args):
        raise TypeError("MetricArray does not support `index()`")

    def pop(self, *args):
        raise TypeError("MetricArray does not support `pop()`")

    def remove(self, *args):
        raise TypeError("MetricArray does not support `remove()`")

    def reverse(self, *args):
        raise TypeError("MetricArray does not support `reverse()`")

    def sort(self, *args):
        raise TypeError("MetricArray does not support `sort()`")


cdef class Set(object):
    """The metric set

    The application obtains a set by either:
    - directly create it using Set() constructor, or
    - `Xprt.lookup()` a remote LDMS set.

    The following is an example of opertaions on the locally create set:
    >>> s = ldms.Set("my_set", my_schema)
    >>> s.producer = "myhost" # set the producer name
    >>> s.publish() # make the set available to the peer
    >>> s.transaction_begin() # begin changing metric values
    >>> s[0] = 'x' # assuming that metric 0 is of single char
    >>> s[11] = 'haha' # assuming that metric 11 is str
    >>> s[12][2] = 25 # assuming that metric 12 is an array of int
    >>> s["some_key"] = 2 # assuming that "some_key" is the name of a metric
    >>> s.transaction_end()

    To delete the set, simply:
    >>> s.delete()

    To obtain a set from a remote peer:
    >>> s = x.lookup("my_set") # x is the Xprt object
    >>> s.update() # update the set, getting the latest data from the peer

    For both locally-created sets and lookup sets, the application can get
    metric values using metric ID (index), name or slice. In the case of array
    metric type, sub index also works.
    >>> s[0] # access by ID
    >>> s[0:5] # access by slice .. this yield list of metrics 0..4
    >>> s[12][2] # if metric 12 is an array, get the element index 2
    >>> s[12] # or access the entire array as list at once

    The following is some useful functions that make Set behaves like dict/list.
    >>> len(s) # yield the number of metrics in the set
    >>> s.keys() # an iterator over metric names (imitate dict.keys())
    ...          # ordered by metric index
    >>> s.values() # an iterator over metric values (imitate dict.values())
    ...            # ordered by metric index
    >>> s.items() # an iterator over (key, value) pair of metrics
    ...           # similar to dict.items(), but ordered by metric index
    >>> s.as_dict() # generate a dictionary { metric_key: metric_value }
    >>> s.as_list() # generate a list [ metric_value ]
    """
    cdef ldms_set_t rbd
    cdef sem_t _sem
    cdef int _update_rc
    # getter/setter for each metric
    cdef list _getter
    cdef list _setter

    def __cinit__(self, *args, **kwargs):
        self.rbd = NULL
        sem_init(&self._sem, 0, 0)

    def __init__(self, str name, Schema schema,
                       int uid=0, int gid=0,
                       int perm=0o777, Ptr set_ptr=None):
        self._getter = list()
        self._setter = list()
        if set_ptr:
            self.rbd = <ldms_set_t>set_ptr.c_ptr
        elif schema:
            if not name:
                raise AttributeError("Missing `name` parameter")
            uid = uid if uid else os.geteuid()
            gid = gid if gid else os.getegid()
            self.rbd = ldms_set_new_with_auth(BYTES(name), schema._schema,
                                              uid, gid, perm)
            if not self.rbd:
                raise RuntimeError("Set creation error: {}"\
                                   .format(ERRNO_SYM(errno)))
        else:
            raise AttributeError("Requires `name` and `schema`")
        cdef ldms_value_type t
        for i in range(0, len(self)):
            t = ldms_metric_type_get(self.rbd, i)
            if ldms_type_is_array(t) and t != LDMS_V_CHAR_ARRAY:
                ma = MetricArray(self, i)
                self._getter.append(ma)
                self._setter.append(ma)
            else:
                self._getter.append(METRIC_GETTER_TBL[t])
                self._setter.append(METRIC_SETTER_TBL[t])

    def __del__(self):
        if self.rbd:
            ldms_set_put(self.rbd)

    def __iter__(self):
        """iter(self) - iterates over keys (metric names) of the set"""
        return self.keys()

    def publish(self):
        """S.publish() - make the set available to LDMS peers"""
        cdef rc = ldms_set_publish(self.rbd)
        if rc:
            raise OSError(rc, "ldms_set_publish() failed: {}" \
                              .format(ERRNO_SYM(rc)))

    def unpublish(self):
        """S.unpublish() - make the set unavailable to LDMS peers"""
        cdef rc = ldms_set_unpublish(self.rbd)
        if rc:
            raise OSError(rc, "ldms_set_unpublish() failed: {}" \
                              .format(ERRNO_SYM(rc)))

    def delete(self):
        """S.delete() - delete the set"""
        ldms_set_delete(self.rbd)

    def transaction_begin(self):
        """S.transaction_begin() - begin data transaction

        The application should call this function before modifying metrics.
        """
        cdef rc = ldms_transaction_begin(self.rbd)
        if rc:
            raise OSError(rc, "ldms_transaction_begin() error: {}" \
                              .format(ERRNO_SYM(rc)))

    def transaction_end(self):
        """S.transaction_end() - end data transaction

        The application shall call this function after it has done modifying
        metrics.
        """
        cdef rc = ldms_transaction_end(self.rbd)
        if rc:
            raise OSError(rc, "ldms_transaction_end() error: {}" \
                              .format(ERRNO_SYM(rc)))

    def keys(self):
        """S.keys() - iterates over keys (metric names) of the set"""
        cdef int i
        for i in range(0, ldms_set_card_get(self.rbd)):
            yield STR(ldms_metric_name_get(self.rbd, i))

    def values(self):
        """S.values() - iterate over metric values of the set"""
        cdef int i
        for i in range(0, ldms_set_card_get(self.rbd)):
            v = self.get_metric(i)
            yield v

    def items(self):
        """S.items() - iterate over metrics, yielding (key, value)"""
        cdef int i
        for i in range(0, ldms_set_card_get(self.rbd)):
            k = STR(ldms_metric_name_get(self.rbd, i))
            v = self.get_metric(i)
            yield (k, v)

    def as_dict(self):
        """S.as_dict() -> dict(S.items())"""
        return dict(self.items())

    def as_list(self):
        """S.as_list() -> list(S.values())"""
        return list(self.values())

    def update(self, cb=None, cb_arg=None):
        """S.update(cb=None, cb_arg=None) - update set data from the remote peer

        If `cb` is not provided, the function call is blocking, i.e. it will not
        return until the update has completed (could be failure or success). If
        the udpate completed in failure, ConnectionError is raised. Otherwise,
        the function simply returns.

        If `cb` is provided, the function became non-blocking. The result of the
        update will be delivered to the `cb` function. Note that the
        non-blocking call can still raise synchronous ConnectionError.

        The signature of the `cb` is as follows:
            def update_cb(lset, flags, arg)
        - `lset` is the set being updated.
        - `flags` is an OR of these bitwise constants: UPD_F_MORE, UPD_F_PUSH,
          UPD_F_PUSH_LAST.  Please consule `ldms.h` for
          more details about these flags.
        - `arg` is the `cb_arg` supplied to `update()`.
        """
        cdef int rc
        tpl = (self, cb, cb_arg)
        Py_INCREF(tpl)
        rc = ldms_xprt_update(self.rbd, update_cb, <void*>tpl)
        if rc: # synchronous error
            Py_DECREF(tpl)
            raise ConnectionError(rc, "ldms_xprt_update() error: {}" \
                                      .format(ERRNO_SYM(rc)))
        if cb:
            return
        with nogil:
            sem_wait(&self._sem)
        if self._update_rc:
            rc = self._update_rc
            raise ConnectionError(rc, "update error: {}".format(ERRNO_SYM(rc)))

    @property
    def instance_name(self):
        """Set instance name"""
        return STR(ldms_set_instance_name_get(self.rbd))

    @property
    def schema_name(self):
        """Schema name"""
        return STR(ldms_set_schema_name_get(self.rbd))

    @property
    def producer_name(self):
        """The name of the ldmsd producing the original set"""
        return STR(ldms_set_producer_name_get(self.rbd))

    @producer_name.setter
    def producer_name(self, val):
        """`producer_name` setter"""
        cdef int rc = ldms_set_producer_name_set(self.rbd, BYTES(val))
        if rc:
            raise OSError(rc, "ldms_set_producer_name_set() error: {}" \
                              .format(ERRNO_SYM(rc)))

    @property
    def card(self):
        """The number of metrics in the set"""
        return ldms_set_card_get(self.rbd)

    def __len__(self):
        """len(S) - the number of metrics in the set"""
        return self.card

    @property
    def uid(self):
        """Set owner UID"""
        return ldms_set_uid_get(self.rbd)

    @property
    def gid(self):
        """Set owner GID"""
        return ldms_set_gid_get(self.rbd)

    @property
    def perm(self):
        """Set permission (Unix-style int)"""
        return ldms_set_perm_get(self.rbd)

    @property
    def meta_sz(self):
        """The size of the meta-data part of the set"""
        return ldms_set_meta_sz_get(self.rbd)

    @property
    def data_sz(self):
        """The size of the data part of the set"""
        return ldms_set_data_sz_get(self.rbd)

    @property
    def name(self):
        """The set instance name"""
        return ldms_set_name_get(self.rbd).decode()

    @property
    def meta_gn(self):
        """meta-data generation number"""
        return ldms_set_meta_gn_get(self.rbd)

    @property
    def data_gn(self):
        """data generation number"""
        return ldms_set_data_gn_get(self.rbd)

    @property
    def is_consistent(self):
        """True if the set is consistent (not in the middle of modification)"""
        return bool(ldms_set_is_consistent(self.rbd))

    @property
    def transaction_timestamp(self):
        """The timestamp of the latest data modification transaction"""
        return ldms_transaction_timestamp_get(self.rbd)

    @property
    def transaction_duration(self):
        """The amount of time used in the latest data modification transaction"""
        return ldms_transaction_duration_get(self.rbd)

    def info_get(self, key):
        """S.info_get(key) - get setinfo[key]"""
        return ldms_set_info_get(self.rbd, BYTES(key)).decode()

    def __getitem__(self, key):
        if type(key) == int:
            if key < 0:
                key += len(self)
            return self.get_metric(key)
        if type(key) == slice:
            return tuple(self.get_metric(i) for i in range(*key.indices(len(self))) )
        return self.get_metric_by_name(key)

    def get_metric_by_name(self, key):
        """S.get_metric_by_name(key) - equivalent to S[key]"""
        cdef int idx = ldms_metric_by_name(self.rbd, BYTES(key))
        if idx < 0:
            raise KeyError("metric '{}' not found".format(key))
        return self.get_metric(idx)

    def get_metric(self, int idx):
        """S.get_metric(idx) - equivalent to S[idx]"""
        cdef ldms_value_type t = ldms_metric_type_get(self.rbd, idx)
        g = self._getter[idx]
        return g(self, idx)

    def __setitem__(self, key, val):
        # key can be int, str or slice
        ktype = type(key)
        if ktype == int:
            self.set_metric(key, val)
        elif ktype == slice:
            rng = range(*key.indices(len(self)))
            if len(rng) != len(val):
                raise ValueError("Mismatch number of assign elements")
            for idx, v in zip(rng, val):
                self.set_metric(idx, v)
        else:
            self.set_metric_by_name(key, val)

    def set_metric_by_name(self, str key, val):
        """S.set_metric_by_name(k, v) - equivalent to S[k]=v"""
        cdef int idx = ldms_metric_by_name(self.rbd, BYTES(key))
        if idx < 0:
            raise KeyError("metric '{}' not found".format(key))
        return self.set_metric(idx, val)

    def set_metric(self, int metric_id, val, sub_idx=None):
        """S.set_metric(i, v, j=None) - equivalent to S[i]=v or S[i][j]=v"""
        cdef ldms_value_type t = ldms_metric_type_get(self.rbd, metric_id)
        cdef int alen
        _setter = self._setter[metric_id]
        if not ldms_type_is_array(t) or t == LDMS_V_CHAR_ARRAY:
            _setter(self, metric_id, val)
            return
        # else, the metric is an array and _setter is MetricArray
        if sub_idx is not None:
            _setter[sub_idx] = val
            return
        # else, set value for entire array
        _setter[:] = val


cdef class Xprt(object):
    """LDMS transport

    The Xprt constructor creates an LDMS transport with the following args:
    - name: The type name of the transport, one of: "sock" (default), "rdma",
            "ugni".
    - auth: The LDMS authentication plugin to use:
            - "none" (default) for no authentication.
            - "ovis" for ovis-implemented pre-shared key
            - "munge" for munge
    - auth_opts: A dictionary containing LDMS authentication plugin options.
                 Please consult the plugin manuals for their options.

    Passive-side simple example:
    >>> x = ldms.Xprt()
    >>> x.listen(port=10000)
    >>> while True:
    ...   newx = x.accept() # this will block


    Active-side simple example:
    >>> x = ldms.Xprt()
    >>> x.connect(host="localhost", port=10000) # blocking conenct
    >>> dlist = x.dir() # LDMS dir to see list of set descriptions
    >>> slist = []
    >>> for d in dlist:
    ...     s = x.lookup(d.name)
    ...     slist.append(s)
    >>> # Or alternatively, lookup by regular expression
    ... slist = x.lookup(".*", ldms.LOOKUP_RE)

    Please see `listen()`, `connect()`, `dir()`, `lookup()` for more
    information. They support both blocking and non-blocking (callback) styles.
    """

    cdef public object ctxt
    """An application context attached to the transport"""

    cdef ldms_t xprt
    # private attributes being `public` for debugging
    cdef sem_t _conn_sem
    cdef int _conn_rc
    cdef public str _conn_rc_msg
    cdef object _conn_cb
    cdef object _conn_cb_arg

    cdef int _dir_rc
    cdef public str _dir_rc_msg
    cdef object _dir_cb
    cdef object _dir_cb_arg
    cdef list _dir_list

    cdef sem_t _lookup_sem
    cdef int _lookup_rc
    cdef public str _lookup_rc_msg

    cdef public object _recv_queue
    cdef public object _accept_queue

    cdef public object _psv_xprts
    # _psv_xprts is a dict(ldms_t :-> Xprt). This is a work around as the newly
    # created passive endpoint inherited callback function and argument from the
    # listening endpoint and LDMS does not have a way (e.g.
    # `ldms_xprt_accept()`) to change the callback function and argument yet.

    def __init__(self, name="sock", auth="none", auth_opts=None,
                       Ptr xprt_ptr=None):
        cdef attr_value_list *avl = NULL;
        cdef int rc;
        if auth is None:
            auth = "none"
        self.ctxt = None
        # conn
        sem_init(&self._conn_sem, 0, 0)
        self._conn_rc = 0
        self._conn_rc_msg = "OK"
        self._conn_cb = None
        self._conn_cb_arg = None
        # dir
        self._dir_rc = 0
        self._dir_rc_msg = "OK"
        self._dir_cb = None
        self._dir_cb_arg = None
        # lookup
        sem_init(&self._lookup_sem, 0, 0)
        self._lookup_rc = 0
        self._lookup_rc_msg = "OK"
        # recv_queue (thread-safe) for synchronous/blocking recv()
        self._recv_queue = Queue()
        # accept queue (thread-safe) for synchronous/blocking accept()
        self._accept_queue = Queue()
        self._psv_xprts = dict()
        if xprt_ptr:
            # wrap the existing ldms_t and done
            self.xprt = <ldms_t>xprt_ptr.c_ptr
            return
        # otherwise create new xprt with the supplied options
        if auth_opts:
            if type(auth_opts) != dict:
                raise TypeError("auth_opts must be a dictionary")
            avl = av_new(len(auth_opts))
            for k, v in auth_opts.items():
                rc = av_add(avl, BYTES(k), BYTES(v))
                if rc:
                    av_free(avl)
                    raise OSError(rc, "av_add() error: {}"\
                                  .format(ERRNO_SYM(rc)))
        self.xprt = ldms_xprt_new_with_auth(BYTES(name), xprt_log, BYTES(auth), avl)
        av_free(avl)
        if not self.xprt:
            raise ConnectionError(errno, "Error creating transport, errno: {}"\
                                         .format(ERRNO_SYM(errno)))

    def __del__(self):
        if self.xprt:
            ldms_xprt_put(self.xprt)

    def connect(self, host, port=411, cb=None, cb_arg=None):
        """X.connect(host, port=411, cb=None, cb_arg=None)

        Connect to the remote LDMS peer

        Arguments:
        - host (str): The hostname (or IP address in string) to connect to.
        - port (int): The peer port number.
        - cb (callable): The callback function.
        - cb_arg (object): The application argument to `cb()`.


        If `cb` is `None`, `connect()` is a blocking function, i.e. it will not
        return until the conenction resolved in either success or failure. In
        the case of success, it simply returns. Otherwise, a ConnectionError is
        raised.

        If `cb` is provided, the `connect()` function becomes non-blocking and
        just returns. Note that it can still synchronously raise
        ConnectionError. The callback is called to deliver transport events
        with the following
        args:
        - xprt (Xprt): The transport object.
        - event (XprtEvent): An object describing an event from the transport.
        - arg (object): The `cb_arg` supplied to the `connect()` function.
        """
        cdef int rc
        self._conn_cb = cb
        self._conn_cb_arg = cb_arg
        rc = ldms_xprt_connect_by_name(self.xprt, BYTES(host), BYTES(port),
                                       xprt_cb, <void*>self)
        if rc:
            raise ConnectionError(rc, "ldms_xprt_connect_by_name() error: {}" \
                                      .format(ERRNO_SYM(rc)))
        if cb:
            return
        # Else, release the GIL and wait
        with nogil:
            sem_wait(&self._conn_sem)
        if self._conn_rc:
            rc = self._conn_rc
            raise ConnectionError(rc, "Connect error: {}".format(ERRNO_SYM(rc)))

    def listen(self, host="*", port=411, cb=None, cb_arg=None):
        """X.listen(host="*", port=411, cb=None, cb_arg=None)

        Listen on `host:port` for LDMS connections

        Arguments:
        - host (str): The hostname (or IP address in string) to listen to.
                      "*" (default) means no specific address.
        - port (int): The listening port number.
        - cb (callable): The callback function.
        - cb_arg (object): The application argument to `cb()`.

        If `cb` is not provided, the incoming connections are delivered to the
        application via the blocking `Xprt.accept()` function.

        If `cb` is provied, the incoming connections are delivered to the
        callback function with the following arguments:
        - xprt (Xprt): The transport object.
        - event (XprtEvent): An object describing an event from the transport.
                             If the event is EVENT_CONNECTED, the xprt
                             is the newly spawned transport. Otherwise, it is an
                             event on the transports already spawned from the
                             listening xprt.
        - arg (object): The `cb_arg` supplied to the `listen()` function.
        """
        cdef int rc
        self._conn_cb = cb
        self._conn_cb_arg = cb_arg
        rc = ldms_xprt_listen_by_name(self.xprt, BYTES(host), BYTES(port),
                                      passive_xprt_cb, <void*>self)
        if rc:
            raise ConnectionError(rc, "ldms_xprt_listen_by_name() error: {}" \
                                      .format(ERRNO_SYM(rc)))

    def accept(self, timeout=None):
        """X.accept(timeout=None) -> Xprt

        Blocking accept the incoming connections until `timeout` runs out.

        Returns Xprt

        Raises queue.Empty if timeout

        REMARK: This function will block indefinitely if `listen()` was called
                with a callback function.
        """
        return self._accept_queue.get(timeout=timeout)

    def close(self):
        """X.close() - terminate the connection"""
        cdef timespec ts
        if self.xprt:
            ldms_xprt_close(self.xprt)
            self.xprt = NULL
            if self._conn_cb: # has `cb` ==> asynchronous/non-blocking mode
                return
            # timed-wait for a DISCONNECTED event in blocking mode
            with nogil:
                clock_gettime(CLOCK_REALTIME, &ts)
                ts.tv_sec += 1
                sem_timedwait(&self._conn_sem, &ts)

    def dir(self, cb=None, cb_arg=None, flags=0):
        """X.dir(cb=None, cb_arg=None, flags=0) - perform an LDMS dir operation

        Arguments:
        - cb (callable): The callback function.
        - cb_arg (object): The application argument to `cb()`.
        - flags (int): 0 or DIR_F_NOTIFY.

        If `cb` is not specified, this function becomes blocking, i.e. it will
        not return until LDMS dir operation completed (successfully or failed).
        If dir completed successfully, the function returns a list of DirSet
        which contains set directory information. If dir completed with a
        failure, ConnectionError is raised.

        If `cb` is specified, the `cb` function is called to deliver dir
        operation results with the following args:
        - xprt (Xprt): The transport object.
        - status (int): The status of dir operation (non-zero means error).
        - dir_data (DirData): The data of the dir result (see DirData).
        - args (object): The `cb_arg` supplied to `dir()`.

        If `flags` is 0, the `cb` is only called to deliver DIR_LIST, possibly
        multiple times until dd.more==0.

        If `flags` is DIR_F_NOTIFY, other DIR events (DIR_ADD, DIR_DEL,
        DIR_UPD) will also be delivered to `cb()`.
        """
        cdef int rc
        self._dir_rc = EPIPE
        self._dir_cb = cb
        self._dir_cb_arg = cb_arg
        self._dir_list = list()
        if not cb:
            flags = 0
        rc = ldms_xprt_dir(self.xprt, dir_cb, <void*>self, flags)
        if rc:
            raise ConnectionError(rc, "ldms_xprt_dir() error: {}" \
                                      .format(ERRNO_SYM(rc)))
        if cb:
            return
        with nogil:
            sem_wait(&self._conn_sem)
        if self._dir_rc:
            rc = self._dir_rc
            raise ConnectionError(rc, "dir callback status: {}" \
                                      .format(ERRNO_SYM(rc)))
        return self._dir_list

    def lookup(self, name, flags=0, cb=None, cb_arg=None):
        """X.lookup(name, flags=0, cb=None, cb_arg=None)

        Perform an LDMS lookup operation

        Arguments:
        - name (str): The instance name, schema name, or regular expression
                      depending on the `flags`.
        - flags (int): One of the following values
            - LOOKUP_BY_INSTANCE (default): to lookup the set by instance.
              The `name` argument is treated as set instance name.
            - LOOKUP_BY_SCHEMA: to lookup using schema name. The `name`
              argument is treated as schema name.
            - LOOKUP_RE: to lookup using regular expression matching
              instance names.
        - cb (callable): the callback function
        - cb_arg (object): the callback argument for application use

        If `cb` is not given (or None), `lookup()` becomes blocking. It waits
        until lookup operation completed (either successfully or failed). If the
        lookup completed with a failure, ConnectionError is raised. Otherwise,
        it returns a single Set object if `flags` is LOOKUP_BY_INSTANCE
        (default) or a list of Set matching the given condition (schema or
        regular expression).

        If `cb` is given, `lookup()` becomes non-blocking. It returns right away
        after successfully requesting a lookup. The function could still raise
        ConnectionError if it experienced a synchronous error. The results of
        the lookup operation will be delivered to the application by calling the
        given callback function with the following arguments:
        - xprt (Xprt): the transport performing the lookup.
        - status (int): 0 for no error, non-zero for error.
        - more (int): 0 indicates that this is the last set matching the
                        criteria, or
                      1 indicates that there will be more sets matching the
                        given criteria.
        - lset (Set): The LDMS Set handle (see Set).
        - arg (object): The `cb_arg` supplied to `Xprt.lookup()` by the
                        application.
        """
        cdef int rc
        slist = list()
        tpl = (self, cb, cb_arg, slist)
        Py_INCREF(tpl)
        rc = ldms_xprt_lookup(self.xprt, BYTES(name), flags,
                              lookup_cb, <void*>tpl)
        if rc:
            # synchronous error
            raise ConnectionError(rc, "ldms_xprt_lookup() error: {}" \
                                      .format(ERRNO_SYM(rc)))
        if cb:
            return
        # else, release the GIL and wait
        with nogil:
            sem_wait(&self._lookup_sem)
        if self._lookup_rc:
            rc = self._lookup_rc
            raise ConnectionError(rc, "lookup callback status: {}" \
                                      .format(ERRNO_SYM(rc)))
        if (flags & (LDMS_LOOKUP_BY_SCHEMA|LDMS_LOOKUP_RE)) or len(slist) > 1:
            return slist
        if slist:
            return slist[0]
        raise KeyError("Set not found")

    def send(self, bytes data):
        """X.send(bytes) - send data to peer"""
        cdef int rc
        cdef int data_len = len(data)
        cdef char *c_data = data
        with nogil:
            rc = ldms_xprt_send(self.xprt, c_data, data_len)
        if rc:
            raise ConnectionError(rc, "ldms_xprt_send() error: {}" \
                                      .format(ERRNO_SYM(rc)))

    def recv(self, timeout = None):
        """X.recv(timeout=None) -> bytes

        Blocking-receive data from peer

        Returns `bytes` the data received from the peer.

        Raises `queue.Empty` if the timeout occurs.

        REMARK: Only use this function if `Xprt.connect()` or `Xprt.listen()`
                was called without a callback function. If the callback was
                given in `Xprt.connect()` or `Xprt.listen()` the
                EVENT_RECV is delivered to the callback function and
                this function will just be blocked indefinitely.
        """
        if self._conn_cb:
            raise ValueError("Bad `Xprt.recv()` call. "
                    "The callback has been supplied to `connect()`. "
                    "The message will be delivered asynchronously via the "
                    "callback function.")
        return self._recv_queue.get(timeout=timeout)

    @property
    def msg_max(self):
        """Maximum length of send/recv message"""
        return ldms_xprt_msg_max(self.xprt)

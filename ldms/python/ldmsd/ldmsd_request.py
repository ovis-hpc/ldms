#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2016 Sandia Corporation. All rights reserved.
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
#      Neither the name of Sandia nor the names of any contributors may
#      be used to endorse or promote products derived from this software
#      without specific prior written permission.
#
#      Neither the name of Open Grid Computing nor the names of any
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
#      Modified source versions must be plainly marked as such, and
#      must not be misrepresented as being the original software.
#
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
#######################################################################
import sys
import struct
import socket

class LDMSD_Except(Exception):
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)

class LDMSD_Req_Attr(object):
    def __init__(self, attr_id, value):
        self.attr_id = attr_id
        # One is added to account for the terminating zero
        self.attr_len = int(len(value)+1)
        self.attr_value = value
        self.fmt = 'iii' + str(self.attr_len) + 's'
        self.packed = struct.pack(self.fmt, 1, self.attr_id,
                                  self.attr_len, self.attr_value)

    def __len__(self):
        return len(self.packed)

    def pack(self):
        return self.packed

class LDMSD_Request(object):
    CLI = 1
    EXAMPLE = 2
    PRDCR_STATUS = 0x100 + 4
    PRDCR_METRIC_SET = 0x100 + 5
    STRGP_STATUS = 0x200 + 4
    UPDTR_STATUS = 0x300 + 4
    PLUGN_STATUS = 0x500 + 4
    SOM_FLAG = 1
    EOM_FLAG = 2
    message_number = 1
    header_size = 20
    def __init__(self, command, message=None, attrs=None):
        self.message = message
        self.request_size = self.header_size
        if message:
            self.request_size += len(self.message)
        self.message_number = LDMSD_Request.message_number
        # Compute the extra size occupied by the attributes and add it
        # to the request size in the request header
        if attrs:
            for attr in attrs:
                self.request_size += len(attr)
            # Account for size of terminating 0
            self.request_size += 4
        self.request = struct.pack('iiiii', -1,
                                   LDMSD_Request.SOM_FLAG | LDMSD_Request.EOM_FLAG,
                                   self.message_number, command,
                                   self.request_size)
        # Add the attributes after the message header
        if attrs:
            for attr in attrs:
                self.request += attr.pack()
            self.request += struct.pack('i', 0) # terminate list
        # Add any message payload
        if message:
            self.request += message
        self.response = ""
        LDMSD_Request.message_number += 1

    def send(self, socket):
        rc = socket.sendall(bytes(self.request))
        if rc:
            raise LDMSD_Except("Error {0} sending request".format(rc))

    def receive(self, socket):
        self.response = ""
        while True:
            hdr = socket.recv(self.header_size)
            (marker, flags, msg_no, cmd_id, rec_len) = struct.unpack('iiiii', hdr)
            if marker != -1:
                raise LDMSD_Except("Invalid response format")
            data = socket.recv(rec_len - self.header_size)
            self.response += data
            if flags & LDMSD_Request.EOM_FLAG:
                break
        return self.response

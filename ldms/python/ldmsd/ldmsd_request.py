#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2020 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
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

import json
import errno
import struct

class LDMSD_Message(object):
    LDMSD_MSG_TYPE_REQ = 1
    LDMSD_MSG_TYPE_RSP = 2

    LDMSD_REC_F_SOM = 1
    LDMSD_REC_F_EOM = 2

    MESSAGE_NO = 1
    LDMSD_REC_HDR_FMT = '!LLLL'
    LDMSD_REC_HDR_SZ = struct.calcsize(LDMSD_REC_HDR_FMT)
    
    def __init__(self, ctrl):
        self.ctrl = ctrl
        self.type = None
        self.msg_no = -1
        self.json_str = ""
        self.json_ent = None
        self.num_rec = 0

    def _newRecord(self, flags, json_str_offset, remaining):
        """Create a record

        offset is offset from the request header
        sz is the size of the data to be sent
        """
        rec_len = self.LDMSD_REC_HDR_SZ + remaining
        hdr =  struct.pack(self.LDMSD_REC_HDR_FMT, self.type, flags, 
                           self.msg_no, rec_len)
        data = struct.pack(str(remaining) + 's', \
                           self.json_str[json_str_offset:json_str_offset+remaining].encode())
        return hdr + data

    def send(self, type, json_ent, json_str):
        self.msg_no = self.MESSAGE_NO
        self.MESSAGE_NO += 1
        
        self.type = type
        if json_str:
            self.json_str += json_str
        if self.json_ent is not None:
            self.json_str = json.dumps(json_ent)
        self.json_str_len = len(self.json_str)

        max_msg = self.ctrl.getMaxRecvLen()
        offset = 0
        leftover = self.json_str_len
        try:
            while True:
                remaining = max_msg - self.LDMSD_REC_HDR_SZ
                if offset == 0:
                    flags = self.LDMSD_REC_F_SOM
                else:
                    flags = 0
                if remaining > leftover:
                    remaining = leftover
                    flags |= self.LDMSD_REC_F_EOM
                record = self._newRecord(flags, offset, remaining)
                offset += remaining
                leftover -= remaining
                self.ctrl.send_command(bytes(record))
                self.num_rec += 1
                if leftover == 0:
                    break
        except:
            raise

    def receive(self):
        json_str = ""
        self.num_rec = 0
        while True:
            record = self.ctrl.receive_response()
            if record is None:
                raise LDMSDRequestException(message="No data received", 
                                            errcode=errno.ECONRESET)
            (self.type, flags, self.msg_no, rec_len) = struct.unpack(self.LDMSD_REC_HDR_FMT, \
                                                              record[:self.LDMSD_REC_HDR_SZ])
            json_str += struct.unpack(str(rec_len - self.LDMSD_REC_HDR_SZ) + 's', 
                                      record[self.LDMSD_REC_HDR_SZ:])[0].decode()
            self.num_rec += 1
            if (flags & self.LDMSD_REC_F_EOM):
                break
        self.json_ent = json.loads(json_str)
        return self
        
    
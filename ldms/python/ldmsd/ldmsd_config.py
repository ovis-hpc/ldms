#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2015-2017 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2015-2017 Sandia Corporation. All rights reserved.
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

from abc import ABCMeta, abstractmethod
from os.path import basename, dirname
import struct
import cmd
from argparse import ArgumentError
"""
@module ldmsd_config

"""

import os
import socket

#:Dictionary contains the cmd_id, required attribute list
#:and optional attribute list of each ldmsd commands. For example,
#:LDMSD_CTRL_CMD_MAP['load']['req_attr'] is the list of the required attributes
#:of the load command.
#:LDMSD_CTRL_CMD_MAP['load']['opt_attr'] is the list of the optional attributes
#:of the load command.
LDMSD_CTRL_CMD_MAP = {'usage': {'req_attr': [], 'opt_attr': ['name']},
                      'load': {'req_attr': ['name']},
                      'term': {'req_attr': ['name']},
                      'config': {'req_attr': ['name', 'producer', 'instance']},
                      'start': {'req_attr': ['name', 'interval'],
                                'opt_attr': ['offset']},
                      'stop': {'req_attr': ['name']},
                      'udata': {'req_attr': ['instance', 'metric', 'udata']},
                      'daemon_exit': {'req_attr': []},
                      'oneshot': {'req_attr': ['name', 'time']},
                      'udata_regex': {'req_attr': ['instance', 'regex', 'base'],
                                      'opt_attr': ['incr']},
                      'version': {'req_attr': [], 'opt_attr': []},
                      'loglevel': {'req_attr': ['level'],},
                      'include': {'req_attr': ['path'] },
                      'env': {'req_attr': []},
                      'logrotate': {'req_attr': [], 'opt_attr': []},
                      ###############################
                      # LDMSD command version 3
                      ###############################
                      ##### Producer Policy #####
                      'prdcr_add': {'req_attr': ['name', 'type', 'xprt', 'host',
                                             'port', 'interval']},
                      'prdcr_del': {'req_attr': ['name']},
                      'prdcr_start': {'req_attr': ['name'],
                                      'opt_attr': ['interval']},
                      'prdcr_stop': {'req_attr': ['name']},
                      'prdcr_start_regex': {'req_attr': ['regex'],
                                            'opt_attr': ['interval']},
                      'prdcr_stop_regex': {'req_attr': ['regex']},
                      'prdcr_status': {'opt_attr': [], 'req_attr': []},
                      'prdcr_set_status': {'opt_attr': ['producer', 'instance', 'schema']},
                      ##### Updater Policy #####
                      'updtr_add': {'req_attr': ['name'],
                                    'opt_attr': ['offset', 'push', 'interval']},
                      'updtr_del': {'req_attr': ['name']},
                      'updtr_match_add': {'req_attr': ['name', 'regex', 'match']},
                      'updtr_match_del': {'req_attr': ['name', 'regex', 'match']},
                      'updtr_prdcr_add': {'req_attr': ['name', 'regex']},
                      'updtr_prdcr_del': {'req_attr': ['name', 'regex']},
                      'updtr_start': {'req_attr': ['name'],
                                      'opt_attr': ['interval', 'offset']},
                      'updtr_stop': {'req_attr': ['name']},
                      'udptr_status': {'req_attr': [], 'opt_attr': []},
                      ##### Storage Policy #####
                      'strgp_add': {'req_attr': ['name', 'plugin', 'container',
                                              'schema']},
                      'strgp_del': {'req_attr': ['name']},
                      'strgp_prdcr_add': {'req_attr': ['name', 'regex']},
                      'strgp_prdcr_del': {'req_attr': ['name', 'regex']},
                      'strgp_metric_add': {'req_attr': ['name', 'metric']},
                      'strgp_metric_del': {'req_attr': ['name', 'metric']},
                      'strgp_start': {'req_attr': ['name']},
                      'strgp_stop': {'req_attr': ['name']},
                      'strgp_status': {'req_attr': [], 'opt_attr': []},
                      ##### Daemon #####
                      'daemon_status': {'req_attr': [], 'opt_attr': []},
                      }

"""@var MAX_RECV_LEN
  The maximum length of the message received back from ldmsd. The default is 4096.
"""
MAX_RECV_LEN = 4096

class ldmsdConfig(object):
    __metaclass__ = ABCMeta
    msg_hdr_len = 24

    @abstractmethod
    def send_command(self, cmd):
        """Send the string of a command to an ldmsd process
        """

    @abstractmethod
    def receive_response(self, recv_len = None):
        """Receive a response from the ldmsd process
        """
        hdr = self.socket.recv(self.msg_hdr_len, socket.MSG_WAITALL)
        (marker, msg_type, flags, msg_no, errcode, rec_len) = struct.unpack('iiiiii', hdr)
        data_len = rec_len - self.msg_hdr_len
        msg = hdr
        if data_len > 0:
            data = self.socket.recv(data_len, socket.MSG_WAITALL)
            msg += data
        return msg

    @abstractmethod
    def close(self):
        """Close the socket
        """
        self.socket.close()

    def get_cmd_attr_list(self, cmd_verb):
        """Return the dictionary of command attributes

        If there are no required/optional attributes, the value of the
        'req'/'opt' key is None. Otherweise, the value is a list of attribute
        names.

        @return: {'req': [], 'opt': []}
        """
        attr_dict = {'req': None, 'opt': None}
        if 'req_attr' in LDMSD_CTRL_CMD_MAP[cmd_verb]:
            if len(LDMSD_CTRL_CMD_MAP[cmd_verb]['req_attr']) > 0:
                attr_dict['req'] = LDMSD_CTRL_CMD_MAP[cmd_verb]['req_attr']
        if 'opt_attr' in LDMSD_CTRL_CMD_MAP[cmd_verb]:
            if len(LDMSD_CTRL_CMD_MAP[cmd_verb]['opt_attr']) > 0:
                attr_dict['opt'] = LDMSD_CTRL_CMD_MAP[cmd_verb]['opt_attr']
        return attr_dict

# class ldmsdUSocketConfig(ldmsdConfig):
#     def __init__(self, ldmsd_sockpath, sockpath = None, max_recv_len = None):
#         self.socket = None
#         if not os.path.exists(ldmsd_sockpath):
#             raise ValueError("{0} doesn't exist.".format(ldmsd_sockpath))
# 
#         self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
# 
#         if max_recv_len is None:
#             self.max_recv_len = MAX_RECV_LEN
#         else:
#             self.max_recv_len = max_recv_len
#         self.ldmsd_sockpath = ldmsd_sockpath
#         self.socket.connect(self.ldmsd_sockpath)
#         self.type = "udomain"
# 
#     def __del__(self):
#         if self.socket is not None:
#             self.socket.close()
# 
#     def setMaxRecvLen(self, max_recv_len):
#         self.max_recv_len = max_recv_len
# 
#     def getMaxRecvLen(self):
#         return self.max_recv_len
# 
#     def getSockPath(self):
#         return self.sockpath
# 
#     def setLdmsdSockPath(self, ldmsd_sockpath):
#         self.ldmsd_sockpath = ldmsd_sockpath
# 
#     def getLdmsdSockPath(self):
#         return self.ldmsd_sockpath
# 
#     def send_command(self, cmd):
#         if self.socket is None:
#             raise Exception("The connection has been closed.")
#         self.socket.sendall(cmd)
# 
#     def receive_response(self, recv_len = None):
#         return ldmsdConfig.receive_response(self, recv_len)
# 
#     def close(self):
#         if self.socket is not None:
#             self.socket.close()
#             self.socket = None
# 
# class ldmsdInetConfig(ldmsdConfig):
#     def __init__(self, host, port, secretword, max_recv_len = MAX_RECV_LEN):
#         self.socket = None
#         self.host = host
#         self.port = port
#         self.max_recv_len = max_recv_len
#         self.secretword = secretword
#         self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
#         self.socket.connect((host, port))
#         buf = self.socket.recv(self.max_recv_len)
#         _chl = struct.unpack('II', buf)
#         if _chl[0] != 0 or _chl[1] != 0:
#             try:
#                 from ovis_lib import ovis_auth
#             except ImportError:
#                 raise ImportError("No module named ovis_lib. Please "
#                                         "make sure that ovis is built with "
#                                         "--enable-swig")
#             # Do authentication
#             auth_chl = ovis_auth.ovis_auth_challenge()
#             auth_chl.lo = _chl[0]
#             auth_chl.hi = _chl[1]
# 
#             if self.secretword is None:
#                 self.socket.close()
#                 raise Exception("The server requires authentication")
#             chl = ovis_auth.ovis_auth_unpack_challenge(auth_chl)
#             auth_psswd = ovis_auth.ovis_auth_encrypt_password(chl, self.secretword)
#             self.socket.send(auth_psswd)
#             s = self.socket.recv(self.max_recv_len)
#             if len(s) == 0:
#                 self.socket.close()
#                 raise Exception("The server closes the connection")
#             self.type = "inet"
# 
#     def __del__(self):
#         if self.socket is not None:
#             self.socket.close()
# 
#     def setMaxRecvLen(self, max_recv_len):
#         self.max_recv_len = max_recv_len
# 
#     def getMaxRecvLen(self):
#         return self.max_recv_len
# 
#     def getHost(self):
#         return self.host
# 
#     def getPort(self):
#         return self.port
# 
#     def send_command(self, cmd):
#         if self.socket is None:
#             raise Exception("The connection has been disconnected")
#         self.socket.sendall(cmd)
# 
#     def receive_response(self, recv_len = None):
#         return ldmsdConfig.receive_response(self, recv_len)
# 
#     def close(self):
#         if self.socket is not None:
#             self.socket.close()
#             self.socket = None

class ldmsdOutbandConfig(ldmsdConfig):
    def __init__(self, secretword, host = None, port = None, sockname = None, max_recv_len = MAX_RECV_LEN):
        self.socket = None
        self.host = host
        self.port = port
        self.sockname = sockname
        self.max_recv_len = max_recv_len
        self.secretword = secretword

        if self.sockname is not None:
            if not os.path.exists(sockname):
                raise ValueError("{0} doesn't exist.".format(sockname))
            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.socket.connect(self.sockname)
        else:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((host, port))
        buf = self.socket.recv(self.max_recv_len)
        _chl = struct.unpack('II', buf)
        if _chl[0] != 0 or _chl[1] != 0:
            try:
                from ovis_lib import ovis_auth
            except ImportError:
                raise ImportError("No module named ovis_lib. Please "
                                        "make sure that ovis is built with "
                                        "--enable-swig")
            # Do authentication
            auth_chl = ovis_auth.ovis_auth_challenge()
            auth_chl.lo = _chl[0]
            auth_chl.hi = _chl[1]

            if self.secretword is None:
                self.socket.close()
                raise Exception("The server requires authentication")
            chl = ovis_auth.ovis_auth_unpack_challenge(auth_chl)
            auth_psswd = ovis_auth.ovis_auth_encrypt_password(chl, self.secretword)
            self.socket.send(auth_psswd)
            s = self.socket.recv(self.max_recv_len)
            if len(s) == 0:
                self.socket.close()
                raise Exception("The server closes the connection")
            self.type = "inet"

    def __del__(self):
        if self.socket is not None:
            self.socket.close()

    def setMaxRecvLen(self, max_recv_len):
        self.max_recv_len = max_recv_len

    def getMaxRecvLen(self):
        return self.max_recv_len

    def getHost(self):
        return self.host

    def getPort(self):
        return self.port

    def send_command(self, cmd):
        if self.socket is None:
            raise Exception("The connection has been disconnected")
        self.socket.sendall(cmd)

    def receive_response(self, recv_len = None):
        return ldmsdConfig.receive_response(self, recv_len)

    def close(self):
        if self.socket is not None:
            self.socket.close()
            self.socket = None


class ldmsdInbandConfig(ldmsdConfig):

    def __init__(self, host, port, xprt, secretword, max_recv_len = MAX_RECV_LEN):
        try:
            from ovis_ldms import ldms
        except:
            raise ImportError("Failed to import ovis_ldms.ldms. "
                              "Please make sure that ldms is built with --enable-swig")
        else:
            self.ldms_module = ldms

        if xprt is None:
            raise ArgumentError("xprt is required to create an LDMS transport")

        self.socket = None
        self.host = host
        self.port = port
        self.max_recv_len = max_recv_len
        self.secretword = secretword
        self.xprt = xprt

        if secretword:
            self.ldms = ldms.ldms_xprt_with_auth_new(self.xprt, None, self.secretword)
        else:

            self.ldms = ldms.ldms_xprt_new(self.xprt, None)

        self.rc = ldms.LDMS_xprt_connect_by_name(self.ldms, self.host, str(self.port))
        if self.rc != 0:
            raise RuntimeError("Failed to connect to ldmsd")
        self.type = "inband"

    def __del__(self):
        pass

    def setMaxRecvLen(self, max_recv_len):
        pass

    def getMaxRecvLen(self):
        return -1

    def getHost(self):
        return self.host

    def getPort(self):
        return self.port

    def send_command(self, cmd):
        if self.ldms is None:
            raise Exception("The connection hasn't been connected.")
        rc = self.ldms_module.ldms_xprt_send(self.ldms, cmd, len(cmd))
        if rc != 0:
            raise RuntimeError("Failed to send the command")

    def receive_response(self, recv_len = None):
        return self.ldms_module.LDMS_xprt_recv(self.ldms)

    def close(self):
        self.ldms_module.ldms_xprt_close(self.ldms)

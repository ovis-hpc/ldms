#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2015-2018 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2015-2018 Open Grid Computing, Inc. All rights reserved.
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
from .ldmsd_request import LDMSD_Request, LDMSD_Req_Attr

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
                      'prdcr_add': {'req_attr': ['name', 'type', 'xprt', 'host', 'port', 'interval'],
                                    'opt_attr' : [ 'auth' ] },
                      'prdcr_del': {'req_attr': ['name']},
                      'prdcr_start': {'req_attr': ['name'],
                                      'opt_attr': ['interval']},
                      'prdcr_stop': {'req_attr': ['name']},
                      'prdcr_start_regex': {'req_attr': ['regex'],
                                            'opt_attr': ['interval']},
                      'prdcr_stop_regex': {'req_attr': ['regex']},
                      'prdcr_status': {'opt_attr': [], 'req_attr': ['name']},
                      'prdcr_set_status': {'opt_attr': ['producer', 'instance', 'schema']},
                      'prdcr_hint_tree': {'req_attr':['name'], 'opt_attr': []},
                      'prdcr_subscribe': {'req_attr':['regex', 'stream'], 'opt_attr': []},
                      'prdcr_unsubscribe': {'req_attr':['regex', 'stream'], 'opt_attr': []},
                      'prdcr_stream_dir' : {'req_attr':['regex'], 'opt_attr':[]},
                      ##### Updater Policy #####
                      'updtr_add': {'req_attr': ['name'],
                                    'opt_attr': ['offset', 'push', 'interval', 'auto_interval']},
                      'updtr_del': {'req_attr': ['name']},
                      'updtr_match_add': {'req_attr': ['name', 'regex', 'match']},
                      'updtr_match_del': {'req_attr': ['name', 'regex', 'match']},
                      'updtr_prdcr_add': {'req_attr': ['name', 'regex']},
                      'updtr_prdcr_del': {'req_attr': ['name', 'regex']},
                      'updtr_start': {'req_attr': ['name'],
                                      'opt_attr': ['interval', 'offset', 'auto_interval']},
                      'updtr_stop': {'req_attr': ['name']},
                      'udptr_status': {'req_attr': [], 'opt_attr': ['name', 'summary']},
                      'updtr_task': {'req_attr': ['name'], 'opt_attr': []},
                      'update_time_stats' : {'req_attr': [], 'opt_attr' : ['name']},
                      ##### Storage Policy #####
                      'strgp_add': {'req_attr': ['name', 'plugin', 'container', 'schema'],
                                    'opt_attr' : [ 'flush', 'decomposition' ] },
                      'strgp_del': {'req_attr': ['name']},
                      'strgp_prdcr_add': {'req_attr': ['name', 'regex']},
                      'strgp_prdcr_del': {'req_attr': ['name', 'regex']},
                      'strgp_metric_add': {'req_attr': ['name', 'metric']},
                      'strgp_metric_del': {'req_attr': ['name', 'metric']},
                      'strgp_start': {'req_attr': ['name']},
                      'strgp_stop': {'req_attr': ['name']},
                      'strgp_status': {'req_attr': [], 'opt_attr': ['name']},
                      'store_time_stats': {'req_attr': [], 'opt_attr':['name']},
                      ##### Plugin #####
                      'plugn_sets': {'req_attr': [], 'opt_attr': []},
                      ##### Streams ###
                      'publish': {'req_attr': ['name'], 'opt_attr': []},
                      'subscribe': {'req_attr': ['name'], 'opt_attr': []},
                      'stream_client_dump': {'req_attr': [], 'opt_attr': []},
                      'stream_dir' : {'req_attr': [], 'opt_attr': []},
                      ##### Daemon #####
                      'daemon_status': {'req_attr': [], 'opt_attr': []},
                      ##### Misc. #####
                      'greeting': {'req_attr': [], 'opt_attr': ['name', 'offset', 'level']},
                      'example': {'req_attr': [], 'opt_attr': []},
                      'set_info': {'req_attr': ['instance'], 'opt_attr': []},
                      'xprt_stats': {'req_attr':[], 'opt_attr': ['reset']},
                      'thread_stats': {'req_attr':[], 'opt_attr': ['reset']},
                      'prdcr_stats': {'req_attr':[], 'opt_attr': []},
                      'set_stats': {'req_attr':[], 'opt_attr': []},
                      'listen': {'req_attr':['xprt', 'port'], 'opt_attr': ['host', 'auth']},
                      'metric_sets_default_authz': {'req_attr':[], 'opt_attr': ['uid', 'gid', 'perm']},
                      ##### Failover. #####
                      'failover_config': {
                                'req_attr': [
                                    'host',
                                    'port',
                                    'xprt',
                                ],
                                'opt_attr': [
                                    'interval',
                                    'peer_name',
                                    'auto_switch',
                                    'timeout_factor',
                                ]
                            },
                      'failover_mod': {'req_attr': [], 'opt_attr': ['auto_switch']},
                      'failover_start': {'req_attr': [], 'opt_attr': []},
                      'failover_stop': {'req_attr': [], 'opt_attr': []},
                      'failover_peercfg_start': {'req_attr': [], 'opt_attr': []},
                      'failover_peercfg_stop': {'req_attr': [], 'opt_attr': []},
                      'setgroup_add': {
                                    'req_attr': ['name'],
                                    'opt_attr': [
                                        'producer',
                                        'interval',
                                        'offset'
                                    ],
                            },
                      'setgroup_mod': {
                                    'req_attr': ['name'],
                                    'opt_attr': [
                                        'interval',
                                        'offset'
                                    ],
                            },
                      'setgroup_del': {'req_attr': ['name'], 'opt_attr': [] },
                      'setgroup_ins': {
                                    'req_attr': ['name'],
                                    'opt_attr': ['instance']
                            },
                      'setgroup_rm':  {
                                    'req_attr': ['name'],
                                    'opt_attr': ['instance']
                            },
                      ##### Authetication. #####
                      'auth_add': {'req_attr': ['name', 'plugin'], 'opt_attr': []},
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
        (marker, msg_type, flags, msg_no, errcode, rec_len) = struct.unpack('!LLLLLL', hdr)
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

class ldmsdInbandConfig(ldmsdConfig):

    CTRL_STATES = ['INIT', 'NEW', 'CONNECTED', 'CLOSED']

    def __init__(self, host, port, xprt, max_recv_len = MAX_RECV_LEN,
                 auth=None, auth_opt=None):
        try:
            from ovis_ldms import ldms
        except:
            raise ImportError("Failed to import ovis_ldms.ldms.")
        else:
            self.ldms_module = ldms

        if xprt is None:
            raise ArgumentError("xprt is required to create an LDMS transport")

        self.ldms = None
        self.socket = None
        self.host = host
        self.port = port
        self.xprt = xprt
        self.state = "INIT"
        self.ldms = ldms.Xprt(name=self.xprt, auth=auth, auth_opts=auth_opt)
        if not self.ldms:
            raise ValueError("Failed to create LDMS transport")

        self.state = "NEW"
        self.max_recv_len = self.ldms.msg_max
        self.ldms.connect(self.host, self.port)
        self.type = "inband"
        self.state = "CONNECTED"

    def __del__(self):
        if self.ldms:
            self.ldms.close()
            self.ldms = None

    def __repr__(self):
        return """<ldmsdInBandConfig host = {0}, port = {1}, \
                    xprt = {2}, state = {3}, max_recv_len = {4}>""".format(
                    self.host, self.port, self.xprt, self.state, self.max_recv_len)

    def getState(self):
        return self.state

    def getMaxRecvLen(self):
        return self.max_recv_len

    def getHost(self):
        return self.host

    def getPort(self):
        return self.port

    def send_command(self, cmd):
        if self.state != "CONNECTED":
            raise RuntimeError("The connection isn't connected.")
        rc = self.ldms.send(cmd)
        if rc != None:
            raise RuntimeError("Failed to send the command. %s" % os.strerror(rc))

    def receive_response(self, recv_len = None):
        if self.state != "CONNECTED":
            raise RuntimeError("The connection isn't connected")
        return self.ldms.recv()

    def comm(self, cmd, attrs=None, **kwargs):
        """Communicate

        Params:
          cmd (str) - The name of the command (e.g. "prdcr_add").
          attrs (dict <str:str>) - The attribute-value dict.
          **kwargs - The attribute-value parameters. The value of the attribute
                     will be encapsulated with `str()`. The attributes in kwargs
                     precede those in `attrs` parameter.

        Return:
          resp

        Example:
          resp = ctrl.comm("prdcr_add", name="lala", xprt="sock",
                           host="localhost", port=12345, type="active",
                           interval=1000000)
        """
        _args = dict()
        if attrs:
            _args.update(attrs)
        _args.update(kwargs)
        _attrs = dict()
        if cmd == "config":
            if 'name' in _args.keys():
                # Let ldmsd handle the case that the attribute 'name' isn't given.
                _attrs["name"] = LDMSD_Req_Attr(attr_name = "name", value = str(_args['name']))
                _args.pop('name')
            l = list()
            for k, v in _args.items():
                l.append("%s=%s" % (str(k), str(v)))
            _attrs["string"] = LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.STRING, value = " ".join(l))
        elif cmd == "env":
            l = list()
            for k, v in _args.items():
                l.append("%s=%s" % (str(k), str(v)))
            _attrs["string"] = LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.STRING, value = " ".join(l))
        else:
            for k, v in _args.items():
                _k = str(k)
                a = LDMSD_Req_Attr(attr_name = _k, value = str(v))
                _attrs[_k] = a
        cmd = LDMSD_Request(command=cmd, attrs = _attrs.values())
        cmd.send(self)
        resp = cmd.receive(self)
        return resp

    def close(self):
        if self.state != "CONNECTED":
            return
        self.ldms.close()
        self.state = "CLOSED"
        self.ldms = None

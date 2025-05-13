#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2020-2023 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2020-2023 Open Grid Computing, Inc. All rights reserved.
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
import os
import struct
import sys
import re
from ovis_ldms import ldms
import time
import json
import errno

#:Dictionary contains the cmd_id, required attribute list
#:and optional attribute list of each ldmsd command. For example,
#:LDMSD_CTRL_CMD_MAP['load']['req_attr'] is the list of the required attributes
#:of the load command.
#:LDMSD_CTRL_CMD_MAP['load']['opt_attr'] is the list of the optional attributes
#:of the load command.
LDMSD_CTRL_CMD_MAP = {'usage': {'req_attr': [], 'opt_attr': ['name']},
                      'load': {'req_attr': ['name'], 'opt_attr' : ['plugin']},
                      'term': {'req_attr': ['name']},
                      'config': {'req_attr': ['name']},
                      'source': {'req_attr': ['path'], 'opt_attr':[]},
                      'start': {'req_attr': ['name', 'interval'],
                                'opt_attr': ['offset', 'exclusive_thread']},
                      'stop': {'req_attr': ['name']},
                      'udata': {'req_attr': ['instance', 'metric', 'udata']},
                      'daemon_exit': {'req_attr': []},
                      'oneshot': {'req_attr': ['name', 'time']},
                      'udata_regex': {'req_attr': ['instance', 'regex', 'base'],
                                      'opt_attr': ['incr']},
                      'version': {'req_attr': [], 'opt_attr': []},
                      'log_level': {'req_attr': ['level'],
                                   'opt_attr': ['name', 'regex']},
                      'include': {'req_attr': ['path'] },
                      'env': {'req_attr': []},
                      'logrotate': {'req_attr': [], 'opt_attr': []},
                      ###############################
                      # LDMSD command version 3
                      ###############################
                      ##### Producer Policy #####
                      'prdcr_add': {'req_attr': ['name', 'type', 'xprt', 'host',
                                                 'port', 'reconnect'],
                                    'opt_attr' : [ 'auth', 'perm', 'interval',
                                                   'rail', 'quota', 'rx_rate',
                                                   'cache_ip' ] },
                      'prdcr_del': {'req_attr': ['name']},
                      'prdcr_start': {'req_attr': ['name'],
                                      'opt_attr': ['interval', 'reconnect']},
                      'prdcr_stop': {'req_attr': ['name']},
                      'prdcr_start_regex': {'req_attr': ['regex'],
                                            'opt_attr': ['interval', 'reconnect']},
                      'prdcr_stop_regex': {'req_attr': ['regex']},
                      'prdcr_status': {'req_attr': [], 'opt_attr':['name']},
                      'prdcr_set_status': {'opt_attr': ['producer', 'instance', 'schema']},
                      'prdcr_hint_tree': {'req_attr':['name'], 'opt_attr': []},
                      'prdcr_subscribe': {'req_attr':['regex'],
                                          'opt_attr': [
                                              'rx_rate', 'stream',
                                              'message_channel'
                                          ]
                                         },
                      'prdcr_unsubscribe': {'req_attr':['regex', 'stream'], 'opt_attr': []},
                      'prdcr_stream_status' : {'req_attr':['regex'], 'opt_attr':[]},
                      ##### Bridge #####
                      'bridge_add': {'req_attr': ['name', 'xprt', 'host', 'port', 'reconnect'],
                                     'opt_attr' : [ 'auth', 'perm', 'rail', 'quota', 'rx_rate' ] },
                      ##### Updater Policy #####
                      'updtr_add': {'req_attr': ['name'],
                                    'opt_attr': ['offset', 'push', 'interval', 'auto_interval', 'perm']},
                      'updtr_del': {'req_attr': ['name']},
                      'updtr_match_add': {'req_attr': ['name', 'regex', 'match']},
                      'updtr_match_del': {'req_attr': ['name', 'regex', 'match']},
                      'updtr_match_list': {'req_attr': [], 'opt_attr': ['name']},
                      'updtr_prdcr_add': {'req_attr': ['name', 'regex']},
                      'updtr_prdcr_del': {'req_attr': ['name', 'regex']},
                      'updtr_start': {'req_attr': ['name'],
                                      'opt_attr': ['interval', 'offset', 'auto_interval']},
                      'updtr_stop': {'req_attr': ['name']},
                      'updtr_status': {'req_attr': [], 'opt_attr': ['name', 'summary', 'reset']},
                      'updtr_task': {'req_attr': ['name'], 'opt_attr': []},
                      'update_time_stats' : {'req_attr': [], 'opt_attr' : ['name', 'reset']},
                      ##### Storage Policy #####
                      'strgp_add': {'req_attr': ['name', 'plugin', 'container'],
                                    'opt_attr' : ['schema', 'regex', 'flush', 'decomposition', 'perm' ] },
                      'strgp_del': {'req_attr': ['name']},
                      'strgp_prdcr_add': {'req_attr': ['name', 'regex']},
                      'strgp_prdcr_del': {'req_attr': ['name', 'regex']},
                      'strgp_metric_add': {'req_attr': ['name', 'metric']},
                      'strgp_metric_del': {'req_attr': ['name', 'metric']},
                      'strgp_start': {'req_attr': ['name']},
                      'strgp_stop': {'req_attr': ['name']},
                      'strgp_status': {'req_attr': [], 'opt_attr': ['name']},
                      'store_time_stats': {'req_attr': [], 'opt_attr':['name', 'reset']},
                      ##### Plugin #####
                      'plugn_sets': {'req_attr': [], 'opt_attr': ['name']},
                      'plugn_status': {'req_attr': [], 'opt_attr': ['name']},
                      ##### Streams ###
                      'publish': {'req_attr': ['name'], 'opt_attr': []},
                      'subscribe': {'req_attr': ['name'], 'opt_attr': []},
                      'stream_client_dump': {'req_attr': [], 'opt_attr': []},
                      'stream_status' : {'req_attr': [], 'opt_attr': ['reset']},
                      'stream_disable' : {'req_attr': [], 'opt_attr':[]},
                      'msg_stats' : {'req_attr': [], 'opt_attr': ['regex', 'stream', 'json', 'reset']},
                      'msg_client_stats' : {'req_attr': [], 'opt_attr': ['json', 'reset']},
                      'msg_disable' : {'req_attr': [], 'opt_attr':[]},
                      ##### Daemon #####
                      'daemon_status': {'req_attr': [], 'opt_attr': ['thread_stats']},
                      ##### Misc. #####
                      'greeting': {'req_attr': [], 'opt_attr': ['name', 'offset', 'level', 'test', 'path']},
                      'example': {'req_attr': [], 'opt_attr': []},
                      'dump_cfg': {'req_attr':['path'], 'opt_attr': []},
                      'set_info': {'req_attr': ['instance'], 'opt_attr': []},
                      'xprt_stats': {'req_attr':[], 'opt_attr': ['reset', 'sq_depth']},
                      'thread_stats': {'req_attr':[], 'opt_attr': ['reset']},
                      'prdcr_stats': {'req_attr':[], 'opt_attr': []},
                      'set_stats': {'req_attr':[], 'opt_attr': ['summary']},
                      'listen': {'req_attr':['xprt', 'port'], 'opt_attr': ['host', 'auth', 'quota', 'rx_rate']},
                      'metric_sets_default_authz': {'req_attr':[], 'opt_attr': ['uid', 'gid', 'perm']},
                      'set_sec_mod' : {'req_attr': ['regex'], 'opt_attr': ['uid', 'gid', 'perm']},
                      'log_status' : {'req_attr' : [], 'opt_attr' : ['name']},
                      'stats_reset' : {'req_attr' : [], 'opt_attr' : ['list']},
                      'profiling' : {'req_attr' : [], 'opt_attr' : ['enable', 'reset']},
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
                                        'offset',
                                        'perm'
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
                      'auth_add': {'req_attr': ['name'],
                                   'opt_attr' : ['plugin', 'auth_opt'] },
                      ##### Sampler Discovery #####
                      'advertiser_add': {'req_attr': ['name', 'xprt', 'host', 'port', 'reconnect'],
                                        'opt_attr' : ['auth', 'perm', 'interval',
                                                      'rail', 'quota', 'rx_rate' ] },
                      'advertiser_del': {'req_attr': ['name'], 'opt_attr': []},
                      'advertiser_start': {'req_attr': ['name'],
                                        'opt_attr' : ['xprt', 'host', 'port',
                                                      'auth', 'perm',
                                                      'reconnect', 'rail',
                                                      'quota', 'rx_rate' ] },
                      'advertiser_stop': {'req_attr': ['name'], 'opt_attr': []},
                      'prdcr_listen_add': {'req_attr': ['name'],
                                           'opt_attr': ['rail', 'ip', 'quota', 'rx_rate',
                                                        'regex', 'disable_start', 'reconnect',
                                                        'advertiser_xprt', 'advertiser_port', 'type',
                                                        'advertiser_auth']},
                      'prdcr_listen_del': {'req_attr': ['name'], 'opt_attr': []},
                      'prdcr_listen_start': {'req_attr': ['name'], 'opt_attr': []},
                      'prdcr_listen_stop': {'req_attr': ['name'], 'opt_attr': []},
                      'prdcr_listen_status': {'req_attr': [], 'opt_attr': []},
                      ##### Quota Group (qgroup) #####
                      'qgroup_config': {
                          'req_attr': [],
                          'opt_attr': [
                              'quota', 'ask_interval', 'ask_amount',
                              'ask_mark', 'reset_interval'
                          ]
                      },
                      'qgroup_member_add': {
                          'req_attr': ['host', 'xprt'],
                          'opt_attr': ['port', 'auth']
                      },
                      'qgroup_member_del': {
                          'req_attr': ['host'],
                          'opt_attr': ['port']
                      },
                      'qgroup_start': {'req_attr': [], 'opt_attr': []},
                      'qgroup_stop': {'req_attr': [], 'opt_attr': []},
                      'qgroup_info': {'req_attr': [], 'opt_attr': []},
                      }

def get_cmd_attr_list(cmd_verb):
    """Return the dictionary of command attributes

    If there are no required/optional attributes, the value of the
    'req'/'opt' key is None. Otherwise, the value is a list of attribute
    names.

    @return: {'req': [], 'opt': []}
    """
    attr_dict = {'req': [], 'opt': []}
    if 'req_attr' in LDMSD_CTRL_CMD_MAP[cmd_verb]:
        if len(LDMSD_CTRL_CMD_MAP[cmd_verb]['req_attr']) > 0:
            attr_dict['req'] = LDMSD_CTRL_CMD_MAP[cmd_verb]['req_attr']
    if 'opt_attr' in LDMSD_CTRL_CMD_MAP[cmd_verb]:
        if len(LDMSD_CTRL_CMD_MAP[cmd_verb]['opt_attr']) > 0:
            attr_dict['opt'] = LDMSD_CTRL_CMD_MAP[cmd_verb]['opt_attr']
    return attr_dict

def fmt_status(msg):
    """
    Format communicator status response string into json object
    """
    if msg is not None and msg != '':
        try:
            msg = json.loads(msg)
        except Exception as e:
            print(f'Error converting {msg} to json object')
            msg = None
    else:
        msg = None
    return msg

class LDMSDRequestException(Exception):
    """Raise when there is an error in the ldmsd request module"""
    def __init__(self, message, errcode, *args):
        self.message = message
        self.errcode = errcode
        super(LDMSDRequestException, self).__init__(message, errcode, *args)

class LDMSD_Req_Attr(object):
    LDMSD_REQ_ATTR_SZ = 12
    LDMSD_REQ_ATTR_DISCRIM_SZ = 4

    NAME = 1
    INTERVAL = 2
    OFFSET = 3
    REGEX = 4
    TYPE = 5
    PRODUCER = 6
    INSTANCE = 7
    XPRT = 8
    HOST = 9
    PORT = 10
    MATCH = 11
    PLUGIN = 12
    CONTAINER = 13
    SCHEMA = 14
    METRIC = 15
    STRING = 16
    UDATA = 17
    BASE = 18
    INCREMENT = 19
    LEVEL = 20
    PATH = 21
    TIME = 22
    PUSH = 23
    TEST = 24
    REC_LEN = 25
    JSON = 26
    PERM = 27
    AUTO_SWITCH = 28
    PEER_NAME = 29
    TIMEOUT_FACTOR = 30
    AUTO_INTERVAL = 31
    UID = 32
    GID = 33
    STREAM = 34
    AUTH = 35
    RESET = 36
    DECOMPOSITION = 37
    RAIL = 38
    QUOTA = 39
    RX_RATE = 40
    SUMMARY = 41
    SIZE = 42
    IP = 43
    ASK_INTERVAL = 44
    ASK_MARK = 45
    ASK_AMOUNT = 46
    RESET_INTERVAL = 47
    XTHREAD = 48
    MSG_CHAN = 49
    LAST = 50

    NAME_ID_MAP = {'name': NAME,
                   'interval': INTERVAL,
                   'interval_us': INTERVAL,
                   'flush' : INTERVAL,
                   'offset': OFFSET,
                   'regex': REGEX,
                   'type': TYPE,
                   'producer': PRODUCER,
                   'instance': INSTANCE,
                   'xprt': XPRT,
                   'advertiser_xprt' : XPRT,
                   'host': HOST,
                   'port': PORT,
                   'advertiser_port' : PORT,
                   'match': MATCH,
                   'plugin': PLUGIN,
                   'container': CONTAINER,
                   'schema': SCHEMA,
                   'string': STRING,
                   'metric': METRIC,
                   'udata': UDATA,
                   'base': BASE,
                   'incr': INCREMENT,
                   'level': LEVEL,
                   'path': PATH,
                   'time': TIME,
                   'push': PUSH,
                   'test': TEST,
                   "REC_LEN": REC_LEN,
                   "json": JSON,
                   'perm': PERM,
                   'auto_switch': AUTO_SWITCH,
                   'peer_name': PEER_NAME,
                   'timeout_factor': TIMEOUT_FACTOR,
                   'auto_interval': AUTO_INTERVAL,
                   'uid': UID,
                   'gid': GID,
                   'stream': STREAM,
                   'period': INTERVAL,
                   'reset': RESET,
                   'auth': AUTH,
                   'advertiser_auth' : AUTH,
                   'decomposition' : DECOMPOSITION,
                   'rail' : RAIL,
                   'quota' : QUOTA,
                   'rx_rate' : RX_RATE,
                   'reconnect' : INTERVAL,
                   'summary' : SUMMARY,
                   'size' : SIZE,
                   'IP' : IP,
                   'ip' : IP,
                   'ask_interval': ASK_INTERVAL,
                   'ask_mark': ASK_MARK,
                   'ask_amount': ASK_AMOUNT,
                   'reset_interval': RESET_INTERVAL,
                   'exclusive_thread': XTHREAD,
                   'message_channel': MSG_CHAN,
                   'TERMINATING': LAST
        }

    ID_NAME_MAP = {NAME : 'name',
                   INTERVAL : 'interval',
                   OFFSET : 'offset',
                   REGEX : 'regex',
                   TYPE : 'type',
                   PRODUCER : 'producer',
                   INSTANCE : 'instance',
                   XPRT : 'xprt',
                   HOST : 'host',
                   PORT : 'port',
                   MATCH : 'match',
                   PLUGIN : 'plugin',
                   CONTAINER : 'container',
                   SCHEMA : 'schema',
                   STRING : 'string',
                   METRIC : 'metric',
                   UDATA : 'udata',
                   BASE : 'base',
                   INCREMENT : 'incr',
                   LEVEL : 'level',
                   PATH : 'path',
                   TIME : 'time',
                   PUSH : 'push',
                   TEST : 'test',
                   REC_LEN : 'REC_LEN',
                   JSON : 'json',
                   PERM : 'perm',
                   AUTO_SWITCH : 'auto_switch',
                   PEER_NAME : 'peer_name',
                   TIMEOUT_FACTOR : 'timeout_factor',
                   AUTO_INTERVAL : 'auto_interval',
                   UID : 'uid',
                   GID : 'gid',
                   STREAM : 'stream',
                   RESET : 'reset',
                   AUTH : 'auth',
                   DECOMPOSITION : 'decomposition',
                   RAIL : 'rail',
                   QUOTA : 'quota',
                   RX_RATE : 'rx_rate',
                   SUMMARY : 'summary',
                   IP : 'ip',
                   ASK_INTERVAL : 'ask_interval',
                   ASK_MARK : 'ask_mark',
                   ASK_AMOUNT : 'ask_amount',
                   RESET_INTERVAL : 'reset_interval',
                   XTHREAD : 'exclusive_thread',
                   MSG_CHAN : 'message_channel',
                   LAST : 'TERMINATING'
        }

    def __init__(self, value = None, attr_name = None, attr_id = None, attr_len = None):
        self.discrim = 1
        if attr_id:
            if attr_id not in self.NAME_ID_MAP.values():
                raise LDMSDRequestException("The attr_id '%d' is not valid" % attr_id, errno.EINVAL)
            self.attr_id = attr_id
            self.attr_name = self.ID_NAME_MAP[attr_id]
        else:
            if attr_name:
                try:
                    self.attr_id = self.NAME_ID_MAP[attr_name]
                except KeyError:
                    raise
                self.attr_name = attr_name
            else:
                # this is the last attribute.
                self.attr_id = self.LAST

        if self.attr_id == self.LAST:
            # terminating attribute
            self.packed = struct.pack("!L", 0)
            self.attr_len = 4
            self.discrim = 0
        else:
            if type(value) is bool:
                value = str(value)
            self.attr_value = value
            if value is None:
                self.attr_len = 0
                self.fmt = '!LLL'
                self.packed = struct.pack(self.fmt, 1, self.attr_id,
                                                    self.attr_len)
            else:
                # One is added to account for the terminating zero
                if attr_len is None:
                    self.attr_len = int(len(value)+1)
                else:
                    self.attr_len = attr_len

                self.fmt = '!LLL'
                if (self.attr_id == self.REC_LEN):
                    self.fmt += 'L'
                else:
                    self.fmt += str(self.attr_len) + 's'
                    self.attr_value = self.attr_value.encode()
                self.packed = struct.pack(self.fmt, 1, self.attr_id,
                                          self.attr_len, self.attr_value)

    def __len__(self):
        return len(self.packed)

    def __repr__(self):
        if self.discrim:
            return "<LDMSD_Req_Attr discrim=%s attr_id=%d attr_len=%d attr_value=%s>" % (
                    self.discrim, self.attr_id, self.attr_len, self.attr_value)
        else:
            return "<LDMSD_Req_Attr discrim=0>"

    @classmethod
    def getDiscrimSize(cls):
        return cls.LDMSD_REQ_ATTR_DISCRIM_SZ

    @classmethod
    def unpack(cls, buf):
        (discrim, ) = struct.unpack('!L', buf[:4])
        if discrim == 0:
            return LDMSD_Req_Attr(attr_id = cls.LAST)

        (discrim, attr_id, attr_len, ) = struct.unpack('!LLL',
                                            buf[:cls.LDMSD_REQ_ATTR_SZ])

        if attr_id == cls.REC_LEN:
            fmt = '!L'
        else:
            fmt = str(attr_len) + 's'

        (attr_value,) = struct.unpack(fmt, buf[cls.LDMSD_REQ_ATTR_SZ:attr_len + cls.LDMSD_REQ_ATTR_SZ])
        if attr_id != cls.REC_LEN:
            attr_value = attr_value.strip(b'\0').decode()

        attr = LDMSD_Req_Attr(value = attr_value, attr_id = attr_id, attr_len = attr_len)
        return attr

    def pack(self):
        return self.packed

class LDMSD_Request(object):
    EXAMPLE = 1
    GREETING = 2
    CFG_CNTR = 3
    DUMP_CFG = 4

    PRDCR_ADD = 0x100
    PRDCR_DEL = 0x100 + 1
    PRDCR_START = 0x100 + 2
    PRDCR_STOP = 0x100 + 3
    PRDCR_STATUS = 0x100 + 4
    PRDCR_START_REGEX = 0x100 + 5
    PRDCR_STOP_REGEX = 0x100 + 6
    PRDCR_SET_STATUS = 0x100 + 7
    PRDCR_HINT_TREE = 0x100 + 8
    PRDCR_SUBSCRIBE = 0x100 + 9
    PRDCR_UNSUBSCRIBE = 0x100 + 10
    PRDCR_STREAM_STATUS = 0x100 + 11
    PRDCR_BRDIGE_ADD = 0x100 + 12
    ADVERTISER_ADD = 0x100 + 13
    ADVERTISER_START = 0x100 + 14
    ADVERTISER_STOP = 0x100 + 15
    ADVERTISER_DEL = 0x100 + 16
    PRDCR_LISTEN_ADD = 0x100 + 17
    PRDCR_LISTEN_DEL = 0x100 + 18
    PRDCR_LISTEN_START = 0x100 + 19
    PRDCR_LISTEN_STOP = 0x100 + 20
    PRDCR_LISTEN_STATUS = 0x100 + 21
    ADVERTISE = 0x100 + 22

    STRGP_ADD = 0x200
    STRGP_DEL = 0x200 + 1
    STRGP_START = 0x200 + 2
    STRGP_STOP = 0x200 + 3
    STRGP_STATUS = 0x200 + 4
    STRGP_PRDCR_ADD = 0X200 + 5
    STRGP_PRDCR_DEL = 0X200 + 6
    STRGP_METRIC_ADD = 0X200 + 7
    STRGP_METRIC_DEL = 0X200 + 8
    STORE_TIME_STATS = 0x200 + 9

    UPDTR_ADD = 0X300
    UPDTR_DEL = 0X300 + 1
    UPDTR_START = 0X300 + 2
    UPDTR_STOP = 0X300 + 3
    UPDTR_STATUS = 0x300 + 4
    UPDTR_PRDCR_ADD = 0X300 + 5
    UPDTR_PRDCR_DEL = 0X300 + 6
    UPDTR_MATCH_ADD = 0X300 + 7
    UPDTR_MATCH_DEL = 0X300 + 8
    UPDTR_TASK = 0x300 + 9
    UPDTR_MATCH_LIST = 0x300 + 10
    UPDATE_TIME_STATS = 0x300 + 11

    SMPLR_ADD = 0X400
    SMPLR_DEL = 0X400 + 1
    SMPLR_START = 0X400 + 2
    SMPLR_STOP = 0X400 + 3

    PLUGN_ADD = 0X500
    PLUGN_DEL = 0X500 + 1
    PLUGN_START = 0X500 + 2
    PLUGN_STOP = 0X500 + 3
    PLUGN_STATUS = 0x500 + 4
    PLUGN_LOAD = 0X500 + 5
    PLUGN_TERM = 0X500 + 6
    PLUGN_CONFIG = 0X500 + 7
    PLUGN_LIST = 0x500 + 8
    PLUGN_SETS = 0x500 + 9

    SET_UDATA = 0x600
    SET_UDATA_REGEX = 0x600 + 1
    VERBOSITY_CHANGE = 0x600 + 2
    DAEMON_STATUS = 0x600 + 3
    VERSION = 0x600 + 4
    ENV = 0x600 + 5
    INCLUDE = 0x600 + 6
    ONESHOT = 0x600 + 7
    LOGROTATE = 0x600 + 8
    EXIT_DAEMON = 0x600 + 9
    SET_ROUTE_OBSOLETE = 0x600 + 11
    XPRT_STATS = 0x600 + 12
    THREAD_STATS = 0x600 + 13
    PRDCR_STATS = 0x600 + 14
    SET_STATS = 0x600 + 15
    LISTEN = 0x600 + 16
    SET_DEFAULT_AUTHZ = 0x600 + 17
    SET_SEC_MOD = 0x600 + 19
    LOG_STATUS = 0x600 + 20
    STATS_RESET = 0x600 + 21
    # IDs 0x600 + 22 to 0x600 + 30 are reserved to match command-line options handlers
    # defined in ldmsd_request.h. These must stay in sync with the C implementation.
    PROFILING = 0x600 + 31

    FAILOVER_CONFIG        = 0x700
    FAILOVER_PEERCFG_START = 0x700  +  1
    FAILOVER_PEERCFG_STOP  = 0x700  +  2
    FAILOVER_MOD           = 0x700  +  3
    FAILOVER_STATUS        = 0x700  +  4

    FAILOVER_START        =  0x770
    FAILOVER_STOP         =  0x770 + 1

    SETGROUP_ADD = 0x800
    SETGROUP_MOD = 0x800 + 1
    SETGROUP_DEL = 0x800 + 2
    SETGROUP_INS = 0x800 + 3
    SETGROUP_RM  = 0x800 + 4

    STREAM_PUBLISH = 0x900
    STREAM_SUBSCRIBE = STREAM_PUBLISH + 1
    STREAM_UNSUBSCRIBE = STREAM_PUBLISH + 2
    STREAM_CLIENT_DUMP = STREAM_PUBLISH + 3
    STREAM_NEW = STREAM_PUBLISH + 4
    STREAM_STATUS = STREAM_PUBLISH + 5
    STREAM_DISABLE = STREAM_PUBLISH + 6

    AUTH_ADD = 0xa00

    QGROUP_CONFIG     = 0xb00
    QGROUP_MEMBER_ADD = QGROUP_CONFIG + 1
    QGROUP_MEMBER_DEL = QGROUP_CONFIG + 2
    QGROUP_START      = QGROUP_CONFIG + 3
    QGROUP_STOP       = QGROUP_CONFIG + 4
    QGROUP_INFO       = QGROUP_CONFIG + 5

    MSG_STATS = 0xc00
    MSG_CLIENT_STATS = MSG_STATS + 1
    MSG_DISABLE = MSG_STATS + 2

    LDMSD_REQ_ID_MAP = {
            'example': {'id': EXAMPLE},
            'greeting': {'id': GREETING},
            'cfg_cntr': {'id': CFG_CNTR},
            'dump_cfg': {'id': DUMP_CFG},

            'prdcr_add': {'id': PRDCR_ADD},
            'prdcr_del': {'id': PRDCR_DEL},
            'prdcr_stop': {'id': PRDCR_STOP},
            'prdcr_status': {'id': PRDCR_STATUS},
            'prdcr_start': {'id': PRDCR_START},
            'prdcr_start_regex': {'id': PRDCR_START_REGEX},
            'prdcr_stop_regex': {'id': PRDCR_STOP_REGEX},
            'prdcr_set_status': {'id': PRDCR_SET_STATUS},
            'prdcr_hint_tree': {'id': PRDCR_HINT_TREE},
            'prdcr_subscribe': {'id': PRDCR_SUBSCRIBE},
            'prdcr_unsubscribe': {'id': PRDCR_UNSUBSCRIBE},
            'prdcr_stream_status' : {'id': PRDCR_STREAM_STATUS},

            'advertiser_add': {'id': ADVERTISER_ADD},
            'advertiser_start': {'id': ADVERTISER_START},
            'advertiser_stop': {'id': ADVERTISER_STOP},
            'advertiser_del': {'id': ADVERTISER_DEL},
            'prdcr_listen_add': {'id': PRDCR_LISTEN_ADD},
            'prdcr_listen_start': {'id': PRDCR_LISTEN_START},
            'prdcr_listen_stop': {'id': PRDCR_LISTEN_STOP},
            'prdcr_listen_del': {'id': PRDCR_LISTEN_DEL},
            'prdcr_listen_status': {'id': PRDCR_LISTEN_STATUS},

            'strgp_add': {'id': STRGP_ADD},
            'strgp_del': {'id': STRGP_DEL},
            'strgp_start': {'id': STRGP_START},
            'strgp_stop': {'id': STRGP_STOP},
            'strgp_status': {'id': STRGP_STATUS},
            'strgp_prdcr_add': {'id': STRGP_PRDCR_ADD},
            'strgp_prdcr_del': {'id': STRGP_PRDCR_DEL},
            'strgp_metric_add': {'id': STRGP_METRIC_ADD},
            'strgp_metric_del': {'id': STRGP_METRIC_DEL},
            'store_time_stats': {'id': STORE_TIME_STATS},

            'updtr_add': {'id': UPDTR_ADD},
            'updtr_del': {'id': UPDTR_DEL},
            'updtr_start': {'id': UPDTR_START},
            'updtr_stop': {'id': UPDTR_STOP},
            'updtr_status': {'id': UPDTR_STATUS},
            'updtr_prdcr_add': {'id': UPDTR_PRDCR_ADD},
            'updtr_prdcr_del': {'id': UPDTR_PRDCR_DEL},
            'updtr_match_add': {'id': UPDTR_MATCH_ADD},
            'updtr_match_del': {'id': UPDTR_MATCH_DEL},
            'updtr_match_list' : {'id' : UPDTR_MATCH_LIST},
            'updtr_task': {'id': UPDTR_TASK},
            'update_time_stats' : {'id': UPDATE_TIME_STATS},

            'start': {'id': PLUGN_START},
            'stop': {'id': PLUGN_STOP},
            'plugn_status': {'id': PLUGN_STATUS},
            'load': {'id': PLUGN_LOAD},
            'term': {'id': PLUGN_TERM},
            'config': {'id': PLUGN_CONFIG},
            'usage': {'id': PLUGN_LIST},

            'plugn_sets': {'id': PLUGN_SETS},

            'udata': {'id': SET_UDATA},
            'udata_regex': {'id': SET_UDATA_REGEX},
            'log_level': {'id': VERBOSITY_CHANGE},
            'daemon_status': {'id': DAEMON_STATUS},
            'version': {'id': VERSION},
            'env': {'id': ENV},
            'include': {'id': INCLUDE},
            'oneshot': {'id': ONESHOT},
            'logrotate': {'id': LOGROTATE},
            'daemon_exit': {'id': EXIT_DAEMON},
            'failover_config'        : {'id' : FAILOVER_CONFIG},
            'failover_peercfg_start' : {'id' : FAILOVER_PEERCFG_START},
            'failover_peercfg_stop'  : {'id' : FAILOVER_PEERCFG_STOP},
            'failover_status'        : {'id' : FAILOVER_STATUS},
            'failover_start'         : {'id' : FAILOVER_START},
            'failover_stop'          : {'id' : FAILOVER_STOP},
            'xprt_stats'    :  {'id' : XPRT_STATS},
            'profiling'    :  {'id' : PROFILING},
            'thread_stats'  :  {'id' : THREAD_STATS},
            'prdcr_stats'   :  {'id' : PRDCR_STATS},
            'set_stats'     :  {'id' : SET_STATS},
            'setgroup_add'  :  {'id':  SETGROUP_ADD},
            'setgroup_mod'  :  {'id':  SETGROUP_MOD},
            'setgroup_del'  :  {'id':  SETGROUP_DEL},
            'setgroup_ins'  :  {'id':  SETGROUP_INS},
            'setgroup_rm'   :  {'id':  SETGROUP_RM},

            'publish'       :  {'id': STREAM_PUBLISH },
            'subscribe'     :  {'id' : STREAM_SUBSCRIBE },
            'unsubscribe'   :  {'id' : STREAM_UNSUBSCRIBE },

            'stream_client_dump'   :  {'id' : STREAM_CLIENT_DUMP },
            'stream_status'    :  {'id' : STREAM_STATUS },
            'stream_disable'   :  {'id' : STREAM_DISABLE },
            'msg_stats'    :  {'id' : MSG_STATS },
            'msg_client_stats'    :  {'id' : MSG_CLIENT_STATS },
            'msg_disable'   :  {'id' : MSG_DISABLE },

            'listen'        :  {'id' : LISTEN },
            'auth_add'      :  {'id' : AUTH_ADD },

            'metric_sets_default_authz' : {'id' : SET_DEFAULT_AUTHZ },
            'set_sec_mod' : {'id' : SET_SEC_MOD },
            'log_status' : {'id' : LOG_STATUS },

            'qgroup_config'     : {'id' : QGROUP_CONFIG     },
            'qgroup_member_add' : {'id' : QGROUP_MEMBER_ADD },
            'qgroup_member_del' : {'id' : QGROUP_MEMBER_DEL },
            'qgroup_start'      : {'id' : QGROUP_START      },
            'qgroup_stop'       : {'id' : QGROUP_STOP       },
            'qgroup_info'       : {'id' : QGROUP_INFO       },
    }

    TYPE_CONFIG_CMD = 1
    TYPE_CONFIG_RESP = 2
    TYPE_LAST = 3

    MARKER = 0xffffffff

    SOM_FLAG = 1
    EOM_FLAG = 2
    message_number = 1
    header_size = 24
    def __init__(self, command=None, command_id=None, message=None, attrs=None):
        if command_id is None and command is None:
            raise Exception("Need either command or command_id")
        if command_id is None:
            if command not in self.LDMSD_REQ_ID_MAP:
                raise KeyError("Command '{0}' is not supported.".format(command))
            command_id = self.LDMSD_REQ_ID_MAP[command]['id']
        self.command_id = command_id
        self.attrs = attrs
        self.message = message
        self.request_size = self.header_size
        self.message_number = LDMSD_Request.message_number

        if message:
            self.request_size += len(self.message)

        # store all packed attribute value pairs
        self.packed_attrs = b""  # excluding the terminating attribute
        self.packed_attrs_sz = 0 # excluding the terminating attribute
        # Compute the extra size occupied by the attributes and add it
        # to the request size in the request header
        try:
            # Aggregate the attributes
            if attrs:
                for attr in attrs:
                    self.packed_attrs += attr.pack()

            self.request_size += len(self.packed_attrs)
            # Account for size of terminating 0
            self.request_size += LDMSD_Req_Attr.getDiscrimSize()

            self.request = struct.pack('!LLLLLL', self.MARKER, self.TYPE_CONFIG_CMD,
                                       LDMSD_Request.SOM_FLAG | LDMSD_Request.EOM_FLAG,
                                       self.message_number, command_id, self.request_size)

            # Add any message payload
            if message:
                self.request += message

            self.request += self.packed_attrs
            self.request += struct.pack('!L', 0) # terminate list

            self.response = {'errcode': None, 'msg': None}
            LDMSD_Request.message_number += 1
        except Exception as e:
            _ , _, c = sys.exc_info()
            msg = str(e) + ' '+str(c.tb_lineno)
            raise LDMSDRequestException(msg, errno.EINVAL)

    def _newRecord(self, flags, offset, sz):
        """Create a record
        offset is offset after the request header
        sz is the size after the offset
        """
        rec_len = self.header_size + sz
        if flags & LDMSD_Request.EOM_FLAG:
            rec_len += LDMSD_Req_Attr.getDiscrimSize()
        hdr = struct.pack('!LLLLLL', self.MARKER, self.TYPE_CONFIG_CMD, flags,
                                   self.message_number, self.command_id, rec_len)
        rec = hdr + self.packed_attrs[offset:offset+sz]
        if flags & LDMSD_Request.EOM_FLAG:
            rec += struct.pack('!L', 0) # adding the terminating attribute
        return rec

    def __repr__(self):
        s = "<LDMSD_Request message_number=%d command_id=%d request_size=%d" % (
                self.message_number, self.command_id, self.request_size)
        if (self.attrs is None) or (len(self.attrs) == 0):
            s += "attrs=[]>"
        else:
            for attr in self.attrs:
                s += "\n    %s" % repr(attr)
            s += "\n]>"
        return s

    def message_number_get(self):
        return self.message_number

    def send(self, ctrl):
        data_len = self.request_size - self.header_size - LDMSD_Req_Attr.getDiscrimSize()
        max_msg = ctrl.getMaxRecvLen()
        offset = 0
        try:
            while True:
                remaining = max_msg - self.header_size - LDMSD_Req_Attr.getDiscrimSize()
                if remaining >= data_len:
                    remaining = data_len
                if offset == 0:
                    flags = LDMSD_Request.SOM_FLAG
                else:
                    flags = 0
                if data_len == remaining:
                    flags |= LDMSD_Request.EOM_FLAG
                rec = self._newRecord(flags, offset, remaining)
                if len(rec) > max_msg:
                    raise RuntimeError("Record size exceeds the maximum of transport message length")
                offset += remaining
                data_len -= remaining
                ctrl.send_command(bytes(rec))
                if data_len == 0:
                    break
        except:
            raise

    def receive(self, ctrl):
        self.response = None
        resp = None
        while True:
            record = ctrl.receive_response()
            if record is None:
                raise LDMSDRequestException(message="No data received", errcode=errno.ECONNRESET)
            (marker, _, msg_flags, _,
             errcode, _) = struct.unpack('!LLLLLL',
                                        record[:self.header_size])

            if marker != self.MARKER:
                raise ValueError("Record is missing the marker")
            data = record[self.header_size:]
            if resp is None:
                resp = data
            else:
                resp += data
            if (msg_flags & LDMSD_Request.EOM_FLAG) != 0:
                break

        attr_list = []

        if resp is not None:
            offset = 0
            while True:
                attr = LDMSD_Req_Attr.unpack(resp[offset:])
                if attr.discrim == 0:
                    break
                if attr.attr_id == attr.REC_LEN:
                    raise LDMSDRequestException(message="The request is too big.",
                                                  errcode=errcode)
                attr_list.append(attr)
                offset += attr.LDMSD_REQ_ATTR_SZ + attr.attr_len
        msg = None
        if len(attr_list) == 1:
            if (attr_list[0].attr_id == LDMSD_Req_Attr.STRING) or (attr_list[0].attr_id == LDMSD_Req_Attr.JSON):
                msg = attr_list[0].attr_value.decode()
        elif len(attr_list) == 0:
            attr_list = None
        return {'errcode': errcode, 'msg': msg, 'attr_list': attr_list}

    def is_error_resp(self, json_obj_resp):
        if json_obj_resp == 0:
            return False

        if len(json_obj_resp) == 1:
            if 'error' in json_obj_resp.keys():
                return True
        elif len(json_obj_resp) > 1:
            if 'error' in json_obj_resp[0].keys():
                return True
        return False

    def resp2json(self, resp):
        return json.dumps(resp)

    @classmethod
    def from_verb_attrs(cls, verb, attrs):
        """Create LDMSD_Request object from verb (str) and attrs (list of str pair)
        verb (str) - the LDMSD command verb
        attrs (list of (str, str)) - the list of attribute-value pairs. For
                positional attribute, the value is None.
        """
        req_attrs = []
        attr_s = []
        for a, v in attrs:
            s = a if v is None else a+"="+v
            if (verb == "config" and a != "name") or (verb == "env"):
                attr_s.append(s)
            else:
                try:
                    attr = LDMSD_Req_Attr(value = v, attr_name = a)
                except KeyError:
                    attr_s.append(s)
                except Exception:
                    raise
                else:
                    req_attrs.append(attr)
        if attr_s:
            attr_str = " ".join(attr_s)
            attr = LDMSD_Req_Attr(value = attr_str, attr_id = LDMSD_Req_Attr.STRING)
            req_attrs.append(attr)
        req_attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.LAST))
        return LDMSD_Request(command = verb, attrs = req_attrs)

    ATTR_RE = re.compile("([^=]+)(?:=(.+))?")

    @classmethod
    def from_str(cls, cmd_str):
        """Parse the cmd_str and make LDMSD_Request object from it
        cmd_str - a string in the `verb arg1=val1 arg2=val2 ...` format.
        """
        tkns = re.split(r"\s+", cmd_str)
        verb = tkns[0]
        av_list = (cls.ATTR_RE.match(x).groups() for x in tkns[1:])
        return cls.from_verb_attrs(verb, av_list)

"""
@module Communicator
"""

class Communicator(object):
    """Implements an interface between a client and an instance of an ldmsd daemon"""
    msg_hdr_len = 24

    INIT        = 1
    CONNECTED   = 2
    CLOSED      = 3
    CTRL_STATES = [ INIT, CONNECTED, CLOSED ]
    CFG_CNTR = 0

    def __init__(self, xprt, host, port, auth=None, auth_opt=None, recv_timeout=5):
        """Create a communicator interface with an LDMS Daemon (LDMSD)

        Parameters:
        - The transport name: 'sock', 'rdma', 'ugni', or 'fabric'
        - The host name
        - The port number

        Keyword Parameters:
        auth     - The authentication plugin name
        auth_opt - Options (if any) to pass to the authentication plugin
        """
        self.INIT = Communicator.INIT
        self.CONNECTED = Communicator.CONNECTED
        self.CLOSED = Communicator.CLOSED
        self.host = host
        self.port = port
        self.xprt = xprt
        self.state = self.INIT
        self.auth = auth
        self.auth_opt = auth_opt
        self.recv_timeout = recv_timeout
        self.ldms = None
        self.ldms = ldms.Xprt(name=self.xprt, auth=auth, auth_opts=auth_opt)

        if not self.ldms:
            raise ValueError(f"Failed to create LDMS transport "
                            f"{xprt}, {host}, {port}, {auth}, {auth_opt}")

        self.max_recv_len = self.ldms.msg_max

    def __del__(self):
        if self.ldms:
            self.ldms.close()
            self.ldms = None

    def __repr__(self):
        return f"<LDMSD_Communicator: host = {self.host}, port = {self.port}, "\
               f"xprt = {self.xprt}, state = {self.state}, "\
               f"max_recv_len = {self.max_recv_len}>"

    def get_cmd_attr_list(self, cmd_verb):
        """Return the dictionary of command attributes

        If there are no required/optional attributes, the value of the
        'req'/'opt' key is None. Otherwise, the value is a list of attribute
        names.

        @return: {'req': [], 'opt': []}
        """
        return get_cmd_attr_list(cmd_verb)

    def reconnect(self, timeout=0):
        if self.ldms:
            self.close()
        self.ldms = ldms.Xprt(name=self.xprt, auth=self.auth, auth_opts=self.auth_opt)
        if self.ldms is None:
            print(f'ldms.Xprt is None')
            return -1
        self.max_recv_len = self.ldms.msg_max
        return self.connect(timeout=timeout)

    def connect(self, timeout=0):
        try:
            if not self.ldms:
                self.ldms = ldms.Xprt(name=self.xprt, auth=self.auth, auth_opts=self.auth_opt)
            rc = self.ldms.connect(self.host, self.port, timeout=timeout)
        except Exception as e:
            if self.auth is not None:
                if self.auth_opt is not None:
                    s = ' '.join([f"{n}={v}" for n, v in self.auth_opt.items()])
                    auth_s = f" with auth {self.auth} {s}"
                else:
                    auth_s = f" with auth {self.auth}"
            else:
                auth_s = ""
            print(f'{e}: connecting to {self.host} on port {self.port} using {self.xprt}{auth_s}')
            self.state = self.CLOSED
            return errno.ENOTCONN
        if rc:
            return 1
        self.type = 'inband'
        self.state = self.CONNECTED
        rc, self.CFG_CNTR = self.getCfgCntr()
        if not rc:
            self.CFG_CNTR = int(self.CFG_CNTR)
        return 0

    def getState(self):
        return self.state

    def getMaxRecvLen(self):
        return self.max_recv_len

    def getHost(self):
        return self.host

    def getPort(self):
        return self.port

    def send_command(self, cmd):
        """This is called by the LDMSRequest class to send a message"""
        if self.state != self.CONNECTED:
            raise ConnectionError("Transport is not connected.")
        return self.ldms.send(cmd)

    def receive_response(self, recv_len = None):
        """This is called by the LDMSRequest class to receive a reply"""
        if self.state != self.CONNECTED:
            raise RuntimeError("Transport is not connected.")
        try:
            rsp = self.ldms.recv(timeout=self.recv_timeout)
        except Exception as e:
            self.close()
            raise ConnectionError(str(e))
        return rsp

    def greeting(self, name=None, offset=None, level=None, test=None, path=None):
        """
        Send a message to the ldmsd. If name=<string> is given, ldmsd will echo the string back.
        If the parameter name is omitted, no responses will be returned. If a keywords 'test' is
        given, ldmsd will respond with the string 'Hi'. e.g. 'greeting test'

        Parameters
        [name]
        [offset]
        [level]
        [test]
        [path]
        """
        attr_list = []
        if name:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name))
        if offset:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.OFFSET, value=offset))
        if test:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.TEST, value=test))
        if level:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.LEVEL, value=level))
        if path:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PATH, value=path))
        if len(attr_list) == 0:
            attr_list = None
        req = LDMSD_Request(command_id=LDMSD_Request.GREETING, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['attr_list']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def dump_cfg(self, path=None):
        """
        Dumps the currently running configuration of a running ldmsd
        Parameters:
        path - The path to write the configuration file
        Returns:
        - status is an errno from the errno module
        - data is an error message if status is !=0 or None
        """
        if path is None or path is True:
            return errno.EINVAL, "Please specify valid configuration path argument"
        req = LDMSD_Request(command_id=LDMSD_Request.DUMP_CFG,
                            attrs = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PATH, value=path) ]
              )
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def auth_add(self, name, plugin=None, auth_opt=None):
        """
        Add an authentication domain
        Parameters:
        name - The authentication domain name
        <plugin-specific attribute> e.g. conf=ldmsauth.conf
        """
        attrs=[ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        if plugin is not None:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PLUGIN, value=plugin))
        if auth_opt:
            if len(auth_opt.split('=')) == 1:
                auth_opt = 'conf='+auth_opt
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.STRING, value=auth_opt))
        req = LDMSD_Request(
                command_id=LDMSD_Request.AUTH_ADD,
                attrs=attrs
                )
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def daemon_exit(self):
        """
        Exits the daemon
        """
        req = LDMSD_Request(
                command='daemon_exit'
              )
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return str(e)

    def failover_config(self, host, xprt, port, auto_switch=None, interval=None, timeout_factor=None, peer_name=None):
        """
        Start LDMSD failover service
        NOTE: After failover service has started, aggregator configuration objects
        (prdcr, updtr, and strgp) are not allowed to be altered (start, stop or reconfigure).

        Parameters:
        host             - host name of the failover partner
        xprt             - transport of the failover parter
        port             - LDMS port of the faijlover parter
        [auto_switch]    - 0|1 Auto switching (failover/failback)
        [interval]       - The interval of the heartbeat
        [timeout_factor] - The heartbeat timeout factor
        [peer_name]      - The failover partner name. If not given,
                           the ldmsd will accept any partner

        Returns:
        - status is an errno from the errno module
        - data is an error message if status is !=0 or None
        """
        attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.HOST, value=host),
                      LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.XPRT, value=xprt),
                      LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PORT, value=port)
                    ]
        if auto_switch:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.AUTO_SWITCH, value=auto_switch))
        if interval:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INTERVAL, value=interval))
        if timeout_factor:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.TIMEOUT_FACTOR, value=timeout_factor))
        if peer_name:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PEER_NAME, value=peer_name))
        req = LDMSD_Request(command_id=LDMSD_Request.FAILOVER_CONFIG, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def failover_start(self):
        """
        Start LDMSD failover service

        Returns:
        - status is an errno from the errno module
        - data is an error message if status is !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.FAILOVER_START)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def failover_stop(self):
        """
        Stop LDMSD failover service

        Returns:
        - status is an errno from the errno module
        - data is an error message if status is !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.FAILOVER_STOP)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def failover_status(self):
        """
        Get LDMSD failover status

        Returns:
        - status is an errno from the errno module
        - data is an error message if status is !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.FAILOVER_STATUS)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def failover_peercfg_start(self):
        """
        Manually start peer configuration
        """
        req = LDMSD_Request(command_id=LDMSD_Request.FAILOVER_PEERCFG_START)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def failover_peercfg_stop(self):
        """
        Manually stop peer configuration
        """
        req = LDMSD_Request(command_id=LDMSD_Request.FAILOVER_PEERCFG_STOP)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def setgroup_add(self, name, producer=None, interval=None, offset=None, perm=None):
        """
        Create a new setgroup

        Parameters:
        name        - The set group name.
        [producer]  - The producer name of the set group.
        [interval]  - The update interval hint (in usec).
        [offset]    - The update offset hint (in usec).
        [perm]      - The permission to modify the setgroup in the future.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        if producer:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PRODUCER, value=producer))
        if interval:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INTERVAL, value=interval))
        if offset:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.OFFSET, value=offset))
        if perm:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PERM, value=perm))
        req = LDMSD_Request(command_id=LDMSD_Request.SETGROUP_ADD, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def setgroup_mod(self, name, interval=None, offset=None):
        """
        Modify attributes of a set group

        Parameters:
        name        - The set group name.
        [interval]  - The update interval hint (in usec).
        [offset]    - The update offset hint (in usec).

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        if interval:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INTERVAL, value=interval))
        if offset:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.OFFSET, value=offset))
        req = LDMSD_Request(command_id=LDMSD_Request.SETGROUP_MOD, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def setgroup_del(self, name):
        """
        Delete the set group

        Parameters:
        name - The set group name.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.SETGROUP_DEL,
                            attrs=[LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def setgroup_ins(self, name, instance):
        """
        Insert sets into the set group

        Parameters:
        name     - Set group name
        instance - Comma-separated list of set instances to add

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.SETGROUP_INS,
                            attrs=[
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INSTANCE, value=instance)
                            ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def setgroup_rm(self, name, instance):
        """
        Remove sets from the set group

        Parameters:
        name    - Set group name
        instance - Comma-separated list of set instances to removed

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.SETGROUP_RM,
                            attrs=[
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INSTANCE, value=instance)
                            ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def stream_client_dump(self):
        """
        Dump stream client info (for debugging)

        No Parameters
        """
        req = LDMSD_Request(command_id=LDMSD_Request.STREAM_CLIENT_DUMP)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def stream_status(self, reset = False):
        """
        Dump stream info

        No parameters
        """
        if reset is None:
            reset = False
        req = LDMSD_Request(command_id=LDMSD_Request.STREAM_STATUS,
                            attrs = [LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.RESET, value=str(reset))])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def msg_stats(self, regex=None, stream=None, reset=None):
        """
        Dump stream stats

        Parameters:
        regex - The regular expression matching the stream names
        stream - The exact match of the stearm name
        reset - Reset the statistics
        """
        attr_list = []
        if regex:
            attr_list.append(LDMSD_Req_Attr(attr_name='regex', value=regex))
        if stream:
            attr_list.append(LDMSD_Req_Attr(attr_name='stream', value=stream))
        if reset:
            attr_list.append(LDMSD_Req_Attr(attr_name='reset', value=reset))
        req = LDMSD_Request(command_id=LDMSD_Request.MSG_STATS, attrs = attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def stream_disable(self):
        """
        Disable stream communication in the daemon

        No parameters
        """
        req = LDMSD_Request(command_id=LDMSD_Request.STREAM_DISABLE)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def msg_client_stats(self, reset=None):
        """
        Dump stream stats

        Parameters:
        regex - The regular expression matching the stream names
        stream - The exact match of the stearm name
        """
        attr_list = []
        if reset is not None:
            attr_list = [LDMSD_Req_Attr(attr_name='reset', value=reset)]
        req = LDMSD_Request(command_id=LDMSD_Request.MSG_CLIENT_STATS, attrs = attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def msg_disable(self):
        """
        Disable LDMS message service in the daemon

        No parameters
        """
        req = LDMSD_Request(command_id=LDMSD_Request.MSG_DISABLE)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)


    def listen(self, xprt, port, host=None, auth=None, quota=None, rx_limit=None):
        """
        Add a listening endpoint

        Parameters:
        xprt - Transport name [sock, rdma, ugni]
        port - Port number
        [host] - Hostname
        [auth] - Authentication domain - If none, the default
                 authentication given the command line
                 (-a and -A) will be used

        """
        attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.XPRT, value=xprt),
                      LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PORT, value=port)
        ]
        if auth:
            attr_list.append(LDMSD_Req_Attr(attr_name='auth', value=auth))
        if quota is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.QUOTA, value=quota))
        if rx_limit is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RX_RATE, value=rx_limit))
        req = LDMSD_Request(
                command='listen',
                attrs=attr_list
              )
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def metric_sets_default_authz(self, uid=None, gid=None, perm=None):
        """
        """
        attr_list = []
        if uid:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.UID, value=uid))
        if gid:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.GID, value=gid))
        if perm:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PERM, value=perm))
        if len(attr_list) == 0:
            attr_list = None
        req = LDMSD_Request(
                command_id=LDMSD_Request.SET_DEFAULT_AUTHZ,
                attrs=attr_list
              )
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def dir_list(self):
        """
        Return the dir sets of this ldms daemon
        """
        try:
            dlist = self.ldms.dir()
            return 0, dlist
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def store_time_stats(self, name=None, reset = False):
        """
        Return the time statistics of a LDMSD storage policy.
        If no name is specified, return statistics of all storgage policies

        Parameters:
        name - The storage policy name
        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is a json object of storage policy statistics, or an error message
        """
        attr_list = [LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.RESET,
                                    value = str(reset))]
        if name:
            attr_list.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.NAME, value=name))
        req = LDMSD_Request(command_id=LDMSD_Request.STORE_TIME_STATS,
                            attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def plugn_load(self, name, plugin=None):
        """
        Load a plugin instance.

        Parameters:
        name  - The instance name
        plugin- The plugin name. If None, 'name' is used

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        attrs=[
            LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)
        ]
        if plugin:
           attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PLUGIN,
                                            value=plugin))
        req = LDMSD_Request(command_id=LDMSD_Request.PLUGN_LOAD,
                            attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def plugn_term(self, name):
        """
        Terminate a plugin instance

        Parameters:
        name  - The plugin instance name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.PLUGN_TERM,
                            attrs=[LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def plugn_config(self, name, cfg_str):
        """
        Configure a plugin instance

        Parameters:
        - The plugin name

        Keyword Parameters:
        - dictionary of plugin specific key/value pairs
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.PLUGN_CONFIG,
                attrs=[ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                        LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.STRING, value=cfg_str)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def plugn_stop(self, name):
        """
        Stop a plugin instance

        Parameters:
        name - The plugin instance name
        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.PLUGN_STOP,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def plugn_status(self, name=None):
        """
        Get the status of a plugin instance

        If a name is not specified, the status is returned for all plugins.

        Parameters:
        name - The plugin instance name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the plugin status
        """
        if name:
            attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        else:
            attr_list = None
        req = LDMSD_Request(command_id=LDMSD_Request.PLUGN_STATUS, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def plugn_sets(self, name=None):
        """
        List the sets provided by a plugin instance

        If name is not provided the sets for each plugin instance are returned.

        Parameters:
        name - The plugin name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the relevant plugin(s) and their sets
        """
        if name:
            attr_list = [LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)]
        else:
            attr_list = None
        req = LDMSD_Request(command_id=LDMSD_Request.PLUGN_SETS, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def set_udata(self, instance, metric, udata):
        """
        Set the user data value for a metric in a metric set. This is typically used
        to convey the component_id to an aggregator

        Parameters:
        instance - The instance name
        metric   - The metric name
        udata    - The desired user-data. This is a 64b unsigned int

        Returns:
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.SET_UDATA,
                            attrs=[
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INSTANCE, value=instance),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.METRIC, value=metric),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.UDATA, value=udata)
                            ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def set_udata_regex(self, instance, regex, base, incr=0):
        """
        Set the user data of multiple metrics using regular expression.
        The user data of the first matched metric is set to the base value.
        The base value is incremented by the given 'incr' value and then
        sets to the user data of the consecutive matched metric and so on.

        Parameters:
        instance - The instance name
        regex    - A regex matching metric names to be set
        base     - A base value of user data (uint64)
        [incr]   - Increment value. The default is 0, if incr is 0 the user data of all
                   matched metrics are set to the base value.
        Returns:
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        if incr is None:
            incr = 0
        req = LDMSD_Request(command_id=LDMSD_Request.SET_UDATA_REGEX,
                            attrs=[
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INSTANCE, value=instance),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.BASE, value=base),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INCR, value=incr)
                            ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def update_time_stats(self, name=None, reset = False):
        attr_list = None
        if reset is None:
            reset = False
        attr_list = [ LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.RESET, value = str(reset))]
        if name:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name))
        req = LDMSD_Request(command_id=LDMSD_Request.UPDATE_TIME_STATS,
                            attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def log_level(self, level, name = None, regex = None):
        """
        Change the verbosity level of ldmsd

        Parameters:
           level  - The valid values are "default", "quiet",
                    or a string of comma-separated list of DEBUG, INFO, WARN, ERROR, and CRITICAL
           name -   A logger name
           regex -  A regular expression match logger names

        Returns:
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        attr_list = [LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.LEVEL, value=level)]
        if name is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.NAME, value = name))
        if regex is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.REGEX, value = regex))
        req = LDMSD_Request(command_id=LDMSD_Request.VERBOSITY_CHANGE,
                            attrs = attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def stats_reset(self, s = None):
        """
        Reset the statistics counters
        """
        if s is not None and len(s) > 0:
            attr_list = [LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.STRING, value = s)]
        else:
            attr_list = []
        req = LDMSD_Request(command_id = LDMSD_Request.STATS_RESET, attrs = attr_list)

        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def getCfgCntr(self):
        req = LDMSD_Request(command_id=LDMSD_Request.CFG_CNTR)
        try:
            req.send(self)
            resp = req.receive(self)
            if resp['errcode']:
                return resp['errcode'], 0
            return resp['errcode'], int(resp['msg'])
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def send_command(self, cmd):
        """This is called by the LDMSRequest class to send a message"""
        if self.state != self.CONNECTED:
            raise ConnectionError("Transport is not connected.")
        return self.ldms.send(cmd)

    def logrotate(self):
        """
        Close the current log file, rename it by appending the timestamp
        in seconds, and then open a new file with the name given at the
        ldmsd command-line

        Returns:
        - status is an errno from the errno module
        - data is an error message is status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.LOGROTATE)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def version(self):
        """
        Get the LDMS version

        Returns
        - status is an errno from the errno module
        - data is the ldms version
        """
        req = LDMSD_Request(command_id=LDMSD_Request.VERSION)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def set_env(self, cfg_str):
        """
        Set ldmsd environment

        Parameters:
        cfg_str - string of attr=value

        Returns:
        - status is an errno from the errno module
        - data is an error msg if status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.ENV,
                            attrs=[LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.STRING, value=cfg_str)])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def include_conf(self, path):
        """
        Include a configuration file

        Parameters
        path  - Path to the configuration file

        Returns:
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.INCLUDE,
                            attrs=[LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PATH, value=path)])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def oneshot(self, name, time):
        """
        Make a sample plugin take a sample at a specific time

        Parameters:
        name - plugin name
        time - Timestamp since epoch. If 'now' is given, teh sample plugin will sample data immediately

        Returns:
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        if time == 'now':
            time = time.time()
        req = LDMSD_Request(command_id=LDMSD_Request.ONESHOT,
                            attrs=[
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.TIME, value=time)
                            ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def stream_publish(self, name, data):
        """
        Publish data to the named stream

        Parameters:
        name  - The stream name
        data  - The data to publish

        Returns:
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.STREAM_PUBLISH,
                            attrs=[
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.STRING, value=data)
                            ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def stream_subscribe(self, name):
        """
        Subscribe to a stream

        Parameters:
        name  - The stream name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message is status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.STREAM_SUBSCRIBE,
                            attrs=[LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def smplr_load(self, name):
        """
        Load an LDMSD sampler plugin.

        Parameters:
        name  - The plugin name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.PLUGN_LOAD,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def smplr_status(self, name = None):
        """
        Query the LDMSD for the status of one or more sampler plugins.

        Keyword Parameters:
        name - If not None (default), the name of the producer to query.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or
          the object containing the producer status
        """
        attrs = None
        if name:
            attrs = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        try:
            req = LDMSD_Request(command_id=LDMSD_Request.PLUGN_STATUS, attrs=attrs)
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']

        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def smplrset_status(self, name=None):
        """
        Return the metric sets provided by a sampler plugin.

        Keyword Parameters:
        name  - The name of the sampler to query. If None (default), all
                samplers are queried.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or
          the object containing the sampler set status
        """
        attrs = None
        if name:
            attrs = [
                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)
            ]
        req = LDMSD_Request(
                command_id=LDMSD_Request.PLUGN_SETS,
                attrs=attrs
        )
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def plugn_start(self, name, interval_us, offset_us=None, xthread=None):
        # If offset unspecified, start in non-synchronous mode
        req_attrs = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                      LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INTERVAL, value=str(interval_us))
                    ]
        if offset_us != None:
            req_attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.OFFSET, value=str(offset_us)))
        if xthread is not None:
            req_attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.XTHREAD,
                                            value=str(xthread)))
        req = LDMSD_Request(
                command_id = LDMSD_Request.PLUGN_START,
                attrs=req_attrs
                )
        try:
            req.send(self)
            resp = req.receive(self)
            err = resp['errcode']
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def _prdcr_add_attr_prep(self, **kwargs):
        attrs = [
            LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.NAME, value=kwargs['name']),
            LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.XPRT, value=kwargs['xprt']),
            LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.HOST, value=kwargs['host']),
            LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.PORT, value=str(kwargs['port']))
        ]
        if 'reconnect' in kwargs.keys() and kwargs['reconnect']:
            attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.INTERVAL, value=str(kwargs['reconnect'])))
        if 'ptype' in kwargs.keys() and kwargs['ptype']:
            attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.TYPE, value=kwargs['ptype']))
        if 'auth' in kwargs.keys() and kwargs['auth']:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.AUTH, value=kwargs['auth']))
        if 'perm' in kwargs.keys() and kwargs['perm']:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PERM, value=str(kwargs['perm'])))
        if 'rail' in kwargs.keys() and kwargs['rail']:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RAIL, value=str(int(kwargs['rail']))))
        if 'quota' in kwargs.keys() and kwargs['quota']:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.QUOTA, value=str(int(kwargs['quota']))))
        if 'rx_rate' in kwargs.keys() and kwargs['rx_rate']:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RX_RATE, value=str(int(kwargs['rx_rate']))))
        if 'cache_ip' in kwargs.keys() and kwargs['cache_ip']:
            attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.IP, value = str(kwargs['cache_ip'])))

        return attrs

    def prdcr_add(self, name, ptype, xprt, host, port, reconnect, auth=None, perm=None,
                  rail=None, quota=None, rx_rate=None, cache_ip=None):
        """
        Add a producer. A producer is a peer to the LDMSD being configured.
        Once started, the LDSMD will attempt to connect to this peer
        periodically until the connection succeeds.

        A producer starts in the STOPPED state. Use the prdcr_start() function
        to start the producer.

        Parameters:
        - The name to give the producer. This name must be unique on the producer.
        - The type of the producer, one of 'passive', or 'active'
        - The transport type, one of 'sock', 'ugni', 'rdma', or 'fabric'
        - The hostname
        - The port number
        - The reconnect interval in microseconds

        Keyword Parameters:
        auth - The authentication domain
        perm - The configuration client permission required to
               modify the producer configuration. Default is None.
        rail - The number of endpoints in a rail. The default is 1.
        quota - The recv quota of our side of the connection (the daemon we
                  are controlling). The default is the daemon's default
                  ('--quota' ldmsd option).
        rx_rate - The recv rate (bytes/second) limit for this connection. The
                  default is -1 (unlimited).
        cache_ip - True: Cache hostname after first successfull resolution;
                   False: Resolve hostname on every connection

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        args_d = {'name': name, 'ptype': ptype, 'xprt': xprt, 'host': host, 'port': port,
                  'reconnect': reconnect, 'auth': auth, 'perm': perm,
                  'rail': rail, 'quota': quota, 'rx_rate': rx_rate,
                  'cache_ip' : cache_ip}
        attrs = self._prdcr_add_attr_prep(**args_d)
        req = LDMSD_Request( command_id = LDMSD_Request.PRDCR_ADD, attrs = attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_del(self, name):
        """
        Delete an LDMS producer. The producer cannot be RUNNING.

        Parameters:
        name - The producer name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.PRDCR_DEL,
                attrs = [
                    LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.NAME, value=name)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_start(self, name, regex=True, reconnect=None, **kwargs):
        """
        Start one or more STOPPED producers

        Parameters:
        - The name of the producer to start. If regex=True (default),
          this is a regular expression.

        Keyword Parameters:
        regex     - True, the 'name' parameter is a regular expression.
                    Default is False.
        reconnect - The reconnect interval in microseconds. If not None, this
                    will override the interval specified when the producer
                    was created. Default is None.
        kwargs   - Additional keyword argument as in prdcr_add().
                    It is to support producer creation if it doesn't exist at start.
                    Currently, only advertiser_start() uses this feature.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        if regex:
            cmd_id = LDMSD_Request.PRDCR_START_REGEX
            name_id = LDMSD_Req_Attr.REGEX
        else:
            cmd_id = LDMSD_Request.PRDCR_START
            name_id = LDMSD_Req_Attr.NAME

        attrs = [
            LDMSD_Req_Attr(attr_id = name_id, value=name),
        ]
        if reconnect:
            attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.INTERVAL,
                                        value = str(reconnect)))

        for key, value in kwargs.items():
            attrs.append(LDMSD_Req_Attr(attr_name = key, value = value))

        req = LDMSD_Request(command_id = cmd_id, attrs = attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)


    def prdcr_stop(self, name, regex=True):
        """
        Stop one or more RUNNING producers

        Parameters:
        - The name of the producer to start. If regex=True (default),
          this is a regular expression.

        Keyword Parameters:
        regex     - True, the 'name' parameter is a regular expression.
                    Default is False.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        if regex:
            cmd_id = LDMSD_Request.PRDCR_STOP_REGEX
            name_id = LDMSD_Req_Attr.REGEX
        else:
            cmd_id = LDMSD_Request.PRDCR_STOP
            name_id = LDMSD_Req_Attr.NAME

        attrs = [
            LDMSD_Req_Attr(attr_id = name_id, value=name),
        ]

        req = LDMSD_Request(command_id = cmd_id, attrs = attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_subscribe(self, regex, stream=None, msg_chan=None, rx_rate='-1'):
        """
        Subscribe to stream data from matching producers

        Parameters:
        - A regular expression matching producer names
        - The name of the stream
        - The recv rate limit

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(command_id = LDMSD_Request.PRDCR_SUBSCRIBE,
                attrs = [
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.STREAM, value=stream),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.MSG_CHAN, value=msg_chan),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RX_RATE, value=str(int(rx_rate)))
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_unsubscribe(self, regex, stream):
        """
        Unsubscribe from stream data from matching producers

        Parameters:
        - A regular expression matching producer names
        - The name of the stream

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message is status !=0 or None
        """
        req = LDMSD_Request(command_id = LDMSD_Request.PRDCR_UNSUBSCRIBE,
                attrs = [
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.STREAM, value=stream)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_stream_status(self, regex):
        """
        Get the stream_dir of the matched prdcr. The connected ldmsd acts as a proxy

        Parameters:
        regex - A regular expression matching prdcr names

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is stream_dir of the matched prdcr or None if rc !=0
        """
        req = LDMSD_Request(command_id = LDMSD_Request.PRDCR_STREAM_STATUS,
                attrs = [
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_status(self, name = None):
        """
        Query the LDMSD for the status of one or more producers.

        Keyword Parameters:
        name - If not None (default), the name of the producer to query.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or
          the object containing the producer status
        """
        attrs = None
        if name:
            attrs = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_STATUS, attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_set_status(self, name = None, instance = None, schema = None):
        """
        Query the LDMSD for one or all producer's set status

        Keyword Parameters:
        name     - If not None (default), the producer to query
        instance - If not None (default), the set's instance name
        schema   - If not None (default), the set's schema name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or
          the object containing the producer sets status
        """
        attrs = []
        if name:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name))
        if instance:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INSTANCE, value=instance))
        if schema:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.SCHEMA, value=schema))
        if len(attrs) == 0:
            attrs = None
        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_SET_STATUS, attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_hint_tree(self, name=None):
        """
        Report the update hints for the given producer name. If no prdcr name is specified,
        report update hints for all prdcrs.

        Parameters:
        [name] - Producer name

        Returns:
        - status is an errno from the errno module
        - data is the prdcr update hints, or an error msg if status !=0 or None
        """
        if name:
            attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        else:
            attr_list = None
        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_HINT_TREE, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def advertiser_add(self, name, xprt, host, port, reconnect, auth=None, perm=None,
                       rail=None, quota=None, rx_rate=None):
        """
        Add an advertiser. An advertiser sends an advertisement to an aggregator
        add it as a producer. Once started, the LDSMD will attempt to
        periodically send a connection request until a connection is established.

        An advertiser starts in the STOPPED state. Use the advertiser_start() function
        to start the advertiser.

        Parameters:
        - The name to give the advertiser. This name must be unique among all advertisement sent to the aggregator.
        - The transport type, one of 'sock', 'ugni', 'rdma', or 'fabric'
        - The aggregator's hostname
        - The aggregator's listening port number
        - The reconnect interval in microseconds

        Keyword Parameters:
        auth - The authentication domain of the remote daemon
        perm - The configuration client permission required to
               modify the producer configuration. Default is None.
        rail - The number of endpoints in a rail. The default is 1.
        quota - The send quota of our side of the connection (the daemon we
                  are controlling). The default is the daemon's default
                  ('-C' ldmsd option).
        rx_rate - The recv rate (bytes/second) limit for this connection. The
                  default is -1 (unlimited).

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        args_d = {'name': name, 'xprt': xprt, 'host': host, 'port': port,
                  'reconnect': reconnect, 'auth': auth, 'perm': perm,
                  'rail': rail, 'quota': quota, 'rx_rate': rx_rate}
        attrs = self._prdcr_add_attr_prep(**args_d)
        attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.TYPE, value="advertiser"))
        req = LDMSD_Request( command_id = LDMSD_Request.ADVERTISER_ADD, attrs = attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def advertiser_start(self, name, xprt=None, host=None, port=None,
                         reconnect=None, auth=None, perm=None,
                         rail=None, quota=None, rx_rate=None):
        """
        Start an advertiser. If the advertiser does not exist, LDMSD will create it.
        In this case, the values of the required attributes in advertiser_add must be given.

        Parameters:
        - The name to give the advertiser. This name must be unique among all advertisement sent to the aggregator.

        Keyword Parameters:
        xprt - The transport type, one of 'sock', 'ugni', 'rdma', or 'fabric'
        host - The aggregator's hostname
        port - The aggregator's listening port number
        reconnect - The reconnect interval in microseconds
        auth - The authentication demain
        perm - The configuration client permission required to
               modify the producer configuration. Default is None.
        rail - The number of endpoints in a rail. The default is 1.
        quota - The send quota of our side of the connection (the daemon we
                  are controlling). The default is the daemon's default
                  ('-C' ldmsd option).
        rx_rate - The recv rate (bytes/second) limit for this connection. The
                  default is -1 (unlimited).

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        args_d = {'name': name, 'xprt': xprt, 'host': host, 'port': port,
                  'reconnect': reconnect, 'auth': auth, 'perm': perm,
                  'rail': rail, 'quota': quota, 'rx_rate': rx_rate}
        attrs = self._prdcr_add_attr_prep(**args_d)
        attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.TYPE, value="advertiser"))
        req = LDMSD_Request( command_id = LDMSD_Request.ADVERTISER_START, attrs = attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def advertiser_stop(self, name):
        req = LDMSD_Request(command_id = LDMSD_Request.ADVERTISER_STOP,
                            attrs = [LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def advertiser_del(self, name):
        req = LDMSD_Request(command_id = LDMSD_Request.ADVERTISER_DEL,
                            attrs = [LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_listen_add(self, name, reconnect=None, disable_start=None, regex=None, ip=None, rail=None, quota=None, rx_rate=None,
                        type="passive", advertiser_xprt=None, advertiser_port=None, advertiser_auth=None):
        """
        Tell an aggregator to wait for advertisements from samplers

        The aggregator automatically adds and starts a producer when it receives
        an advertisement that the peer (sampler) hostname matches the regular expression
        unless the disable_start parameter is specified.

        Parameters:
         - Name of the producer listen
         - Regular expression to match sampler hostnames
         - IP range in the CIDR format

        Return:
        - status is an errno from the errno module
        - data is an error message if status !=0 or None
        """
        attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        if disable_start is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.AUTO_INTERVAL, value=disable_start))
        if regex is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex))
        if ip is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.IP, value=ip))
        if rail is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RAIL, value=rail))
        if quota is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.QUOTA, value=quota))
        if rx_rate is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RX_RATE, value=rx_rate))
        if type is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.TYPE, value=type))
        if advertiser_xprt is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.XPRT, value=advertiser_xprt))
        if advertiser_port is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PORT, value=advertiser_port))
        if advertiser_auth is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.AUTH, value=advertiser_auth))
        if reconnect is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INTERVAL, value=reconnect))

        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_LISTEN_ADD,
                            attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def prdcr_listen_del(self, name):
        """
        Delete a producer listen

        Parameter:
         - Name of the producer listen to be deleted

        Return:
         - Status is an errno from the errno module
         - Data is an error message if status != 0 or None
        """
        attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)]
        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_LISTEN_DEL,
                            attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def prdcr_listen_start(self, name):
        """
        Start a producer listen

        Parameter:
         - Name of the producer listen to be started

        Return:
         - Status is an errno from the errno module
         - Data is an error message if status != 0 or None
        """
        attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)]
        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_LISTEN_START,
                            attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def prdcr_listen_stop(self, name):
        """
        Stop a producer listen

        Parameter:
         - Name of the producer listen to be stopped

        Return:
         - Status is an errno from the errno module
         - Data is an error message if status != 0 or None
        """
        attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)]
        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_LISTEN_STOP,
                            attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def prdcr_listen_status(self):
        """
        Get the status of all producer listen
        """
        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_LISTEN_STATUS)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def updtr_add(self, name, interval=1000000, offset=None, push=None, auto=None, perm=None):
        """
        Add an Updater that will periodically update Producer metric sets either
        by pulling the content or by registering for an update push. The default
        is to pull a set's contents.

        Parameters:
        name      - The update policy name

        Keyword Parameters:
        interval  - The update data collection interval. This is when the
                    push argument is not given. Defaults to 1s
        push      - [onchange|true] 'onchange' means the updater will receive
                    updated set data when the set sampler ends a transaction or
                    explicitly pushes the update. 'true' means the updater
                    will receive an update only when the set source explicitly
                    pushes the update.
                    If `push` is used, `auto_interval` cannot be `true`.
        auto      - [True|False] If True, the updater will schedule
                    set updates according to the update hint. The sets
                    with no hints will not be updated. If False, the
                    updater will schedule the set updates according to
                    the given sample interval. The default is False.
        perm      - The configuration client permission required to
                    modify the updater configuration.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        attrs = [
            LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
        ]
        if not interval and not auto and not push:
            auto = True
        if interval is None:
            interval = 1000000
        attrs += [
             LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INTERVAL, value=str(interval))
        ]
        if offset:
            attrs += [
                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.OFFSET, value=str(offset))
            ]
        if auto:
            attrs += [
                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.AUTO_INTERVAL, value=str(auto))
            ]
        elif push:
            if push != 'onchange' and push != True:
                return errno.EINVAL, "EINVAL"
            attrs += [
                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PUSH, value=str(push))
            ]
        if perm:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PERM, value=str(perm)))
        req = LDMSD_Request(command_id=LDMSD_Request.UPDTR_ADD, attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def updtr_del(self, name):
        """
        Delete an LDMS updater. The updater cannot be RUNNING.

        Parameters:
        name - The updater name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.UPDTR_DEL,
                attrs = [
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def updtr_status(self, name=None, summary=None, reset=None):
        """
        Get the status of all updaters on a producer.

        Parameters:
        name - The name of the producer on which updater status is requested

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the status of updaters on the producer, None if none exist, or an error message if status !=0.
        """
        attrs = []
        if name:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name))
        if summary:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.STRING, value=summary))
        if reset:
            attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RESET, value=reset))
        req = LDMSD_Request(command_id=LDMSD_Request.UPDTR_STATUS, attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def updtr_start(self, name, interval=None, offset=None, auto_interval=None):
        """
        Start a STOPPED updater.

        Parameters:
        - The name of the updater to start.

        Keyword Parameters:
        interval  - The update data collection interval in microseconds.
                    This is required if auto is False.
        auto      - [True|False] If True, the updater will schedule
                    set updates according to the update hint. The sets
                    with no hints will not be updated. If False, the
                    updater will schedule the set updates according to
                    the given sample interval. The default is False.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        attrs = [
            LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
        ]
        if interval:
            if auto_interval:
                return errno.EINVAL, "'auto' is incompatible with 'interval'"
            attrs += [
                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.INTERVAL, value=str(interval)),
            ]
            if offset:
                attrs.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.OFFSET, value=str(offset)))
        elif auto_interval:
            attrs += [
                LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.AUTO_INTERVAL, value=str(auto_interval))
            ]

        req = LDMSD_Request(command_id=LDMSD_Request.UPDTR_START, attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def updtr_stop(self, name):
        """
        Stop a RUNNING updater.

        Parameters:
        - The name of the updater

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.UPDTR_STOP,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def updtr_prdcr_add(self, name, regex):
        """
        Add matching producers to an updater policy. The
        updater must be STOPPED.

        Parameters:
        - The updater name
        - A regular expression matching zero or more producers

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.UPDTR_PRDCR_ADD,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def updtr_prdcr_del(self, name, regex):
        """
        Remove matching producers from an updater policy. The
        updater must be STOPPED.

        Parameters:
        - The updater name
        - A regular expression matching zero or more producers

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.UPDTR_PRDCR_DEL,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def updtr_match_add(self, name, regex, match='schema'):
        """
        Add a match condition that identifies the set that will be
        updated.

        Parameters::
        name  - The update policy name
        regex - The regular expression string
        match - The value with which to compare; if match='inst' (default),
                the expression will match the set's instance name, if
                match='schema', the expression will match the set's
                schema name.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        if match is None:
            match = 'schema'
        req = LDMSD_Request(
                command_id=LDMSD_Request.UPDTR_MATCH_ADD,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.MATCH, value=match)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def updtr_match_del(self, name, regex, match='schema'):
        """
        Remove a match condition from an updater. The updater
        must be STOPPED.

        Parameters::
        name  - The update policy name
        regex - The regular expression string
        match - The value with which to compare; if match='inst' (default),
                the expression will match the set's instance name, if
                match='schema', the expression will match the set's
                schema name.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.UPDTR_MATCH_DEL,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.MATCH, value=match)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def updtr_match_list(self, name=None):
        """
        Return a list of sets that an updater is matched to update.
        name - The update policy name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is a list of updaters and their sets, None if none exist, or an error message if status !=0.
        """
        attr_list = None
        if name:
            attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        req = LDMSD_Request(
                command_id=LDMSD_Request.UPDTR_MATCH_LIST,
                attrs=attr_list
                )
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def updtr_task(self, name=None):
        """
        Report the updater tasks

        Parameters:
        [name] - Updater name, if ommited, reports on all updaters

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the updater tasks, or an error msg if rc !=0 or None
        """
        if name:
            attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        else:
            attr_list = None
        req = LDMSD_Request(command_id=LDMSD_Request.UPDTR_TASK, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def usage(self, name=None):
        ### WIP
        """
        List the usage of the plugins loaded on the server.
        Parameters:
        name        (optional) Name of plugin
        """
        attr_list = []
        if name:
            attr_list.append(LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name))
        req = LDMSD_Request(
                command_id=LDMSD_Request.PLUGN_LIST,
                attrs=attr_list
              )
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_add(self, name, plugin, container, schema=None,
                  regex=None, perm=0o600, flush=None, decomposition=None):
        """
        Add a Storage Policy that will store metric set data when
        updates complete on a metric set.

        Parameters:
        name      - The unique storage policy name.
        plugin    - The name of the storage backend.
        container - The storage backend container name.

        Keyword Parameters:
        schema        - The schema name of the metric set to store. If 'schema' is given, 'regex' is ignored.
        regex         - A regular expression matching set schemas. This must be
                        used with decomposition. Either 'schema' or 'regex' must be given.
        perm          - The permission required to modify the storage policy,
                        default perm=0o600
        flush         - Interval between calls to the storage plugin flush method.
                        By default, the flush method is not called.
        decomposition - The path to a decomposition configuration file
        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        attrs = [
            LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
            LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PLUGIN, value=plugin),
            LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.CONTAINER, value=container),
            LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.PERM, value=str(perm)),
        ]

        if schema is not None:
            attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.SCHEMA, value = schema))
        if regex is not None:
            attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.REGEX, value = regex))
        if decomposition is not None:
            attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.DECOMPOSITION, value = decomposition))
        if flush is not None:
            attrs.append(LDMSD_Req_Attr(attr_name='flush', value=flush))
        req = LDMSD_Request(command_id=LDMSD_Request.STRGP_ADD, attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_del(self, name):
        """
        Delete a storage policy. The storage policy cannot be RUNNING.

        Parameters:
        name - The policy name

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.STRGP_DEL,
                attrs = [
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_start(self, name):
        """
        Start a STOPPED storage policy.

        Parameters:
        - The name of the storage policy to start.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        attrs = [
            LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
        ]
        req = LDMSD_Request(command_id=LDMSD_Request.STRGP_START, attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_stop(self, name):
        """
        Stop a RUNNING storage policy.

        Parameters:
        - The name of the storage policy

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.STRGP_STOP,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_status(self, name=None):
        """
        Get the status of storage policies
        Parameters:
            [name=]      storage policy name
        """
        attr_list = None
        if name:
            attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name) ]
        req = LDMSD_Request(command_id=LDMSD_Request.STRGP_STATUS, attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_prdcr_add(self, name, regex):
        """
        Add matching producers to an storage policy. The
        storage policy must be STOPPED.

        Parameters:
        - The storage policy name
        - A regular expression matching zero or more producers

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.STRGP_PRDCR_ADD,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_prdcr_del(self, name, regex):
        """
        Remove matching producers from an storage policy. The
        storage policy must be STOPPED.

        Parameters:
        - The storage policy name
        - A regular expression matching zero or more producers

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.STRGP_PRDCR_DEL,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.REGEX, value=regex)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_metric_add(self, name, metric_name):
        """
        Add a metric name that will be stored. By default all metrics
        in the schema specified in strgp_add will be stored.

        Parameters::
        - The update policy name
        - The name of the metric to store

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.STRGP_METRIC_ADD,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.METRIC, value=metric_name)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def strgp_metric_del(self, name, metric_name):
        """
        Remove a metric name from the storage policy. The storage policy
        must be STOPPED.

        Parameters:
        - The storage policy name
        - The metric name to remove

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is an error message if status != 0 or None
        """
        req = LDMSD_Request(
                command_id=LDMSD_Request.STRGP_METRIC_DEL,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.NAME, value=name),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.METRIC, value=metric_name)
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def xprt_stats(self, reset=False, level=0):
        """Query the daemon's telemetry data"""
        if reset is None:
            reset = False
        req = LDMSD_Request(
                command_id=LDMSD_Request.XPRT_STATS,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RESET,
                                   value=str(reset)),
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.LEVEL,
                                   value=str(level))
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def profiling(self, enable = None, reset = None):
        attrs = []
        if enable is not None:
            attrs.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.TYPE,
                                         value = enable))
        if reset is not None:
            attrs.append(LDMSD_Req_Attr(attr_id  = LDMSD_Req_Attr.RESET,
                                        value = reset))
        req = LDMSD_Request(
                command_id=LDMSD_Request.PROFILING, attrs=attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def thread_stats(self, reset=False):
        """Query the daemon's I/O thread utilization data"""
        if reset is None:
            reset = False
        req = LDMSD_Request(
                command_id=LDMSD_Request.THREAD_STATS,
                attrs=[
                    LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.RESET,
                                value=str(reset)),
                ])
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def prdcr_stats(self):
        """
        Query the daemon's producer statistics

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's prdcr stats, or an error msg if status !=0 or None
        """
        req = LDMSD_Request(command_id=LDMSD_Request.PRDCR_STATS)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def set_stats(self, summary = False):
        """
        Query the daemon's set statistics

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's set statistics, or an error msg if status !=0 or None
        """
        attr_list = [ LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.SUMMARY, value = str(summary))]
        req = LDMSD_Request(command_id=LDMSD_Request.SET_STATS, attrs = attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            return errno.ENOTCONN, str(e)

    def daemon_status(self, thread_stats=None):
        """
        Query the daemon's status
        Parameters:
        - True/False boolean that returns thread statistics in response if True
        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's current status. if thread_stats is True, it
               also returns the daemon's thread statistics
        """
        attr_list = None
        if thread_stats:
            attr_list = [ LDMSD_Req_Attr(attr_id=LDMSD_Req_Attr.STRING, value='true') ]
        req = LDMSD_Request(command_id=LDMSD_Request.DAEMON_STATUS,
                            attrs=attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            err = resp['errcode']
            if err == 0 and resp['msg'] is not None:
                status = resp['msg']
            else:
                status = None
            return err, status
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def set_sec_mod(self, regex, uid = None, gid = None, perm = None):
        """
        Change the security parameters of the set matched the regular expression
        Parameters:
           Regular expression string
           UID
           GID
           Permissions
        """
        attr_list = [LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.REGEX,
                                    value = regex)]
        if uid is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.UID,
                                            value = uid))
        if gid is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.GID,
                                            value = gid))
        if perm is not None:
            attr_list.append(LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.PERM,
                                            value = perm))
        req = LDMSD_Request(command_id = LDMSD_Request.SET_SEC_MOD,
                            attrs = attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def log_status(self, name = None):
        """
        List the log systems with the log level threashold
        """
        if name is not None:
            attr_list = [LDMSD_Req_Attr(attr_id = LDMSD_Req_Attr.NAME, value = name)]
        else:
            attr_list = []
        req = LDMSD_Request(command_id = LDMSD_Request.LOG_STATUS, attrs = attr_list)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except Exception as e:
            self.close()
            return errno.ENOTCONN, str(e)

    def __comm_routine(self, cmd_id, av_list = list()):
        attrs = [ LDMSD_Req_Attr(attr_id = a, value = v) \
                            for a, v in av_list if v is not None ]
        req = LDMSD_Request(command_id = cmd_id, attrs = attrs)
        try:
            req.send(self)
            resp = req.receive(self)
            return resp['errcode'], resp['msg']
        except ConnectionError as e:
            self.close()
            return errno.ENOTCONN, str(e)
        except Exception as e:
            self.close()
            return -1, str(e)

    def qgroup_config(self, quota:str=None, ask_interval:str=None,
                      ask_amount:str=None, ask_mark:str=None,
                      reset_interval:str=None):
        """
        Configure qgroup.

        Parameters:
        - quota(str): amount of quota in bytes (e.g. '3K').
        - ask_interval(str): time interval to ask quota from members (e.g.  '1s').
        - ask_amount(str): the byte amount to ask members for (e.g. '1K').
        - ask_mark(str): the quota mark to start asking members for more quota
                         (e.g. '1K').
        - reset_interval(str): time interval to reset our quota (e.g. '1s').

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's set statistics, or an error msg if status !=0 or None
        """
        params = [ (LDMSD_Req_Attr.QUOTA,          quota),
                   (LDMSD_Req_Attr.ASK_INTERVAL,   ask_interval),
                   (LDMSD_Req_Attr.ASK_AMOUNT,     ask_amount),
                   (LDMSD_Req_Attr.ASK_MARK,       ask_mark),
                   (LDMSD_Req_Attr.RESET_INTERVAL, reset_interval) ]
        return self.__comm_routine(LDMSD_Request.QGROUP_CONFIG, params)

    def qgroup_member_add(self, host:str, xprt:str, port:str=None, auth:str=None):
        """
        Add a member into the Quota Group (qgroup).

        Parameters:
        - host(str): the host (e.g. 'node1').
        - xprt(str): the transport plugin (e.g. 'sock').
        - port(str): the port (e.g. '12345', default: '411').
        - auth(str): the authentication object for this connection (default; None).

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's set statistics, or an error msg if status !=0 or None
        """
        params = [ (LDMSD_Req_Attr.HOST, host),
                   (LDMSD_Req_Attr.XPRT, xprt),
                   (LDMSD_Req_Attr.PORT, port),
                   (LDMSD_Req_Attr.AUTH, auth) ]
        return self.__comm_routine(LDMSD_Request.QGROUP_MEMBER_ADD, params)

    def qgroup_member_del(self, host:str, port:str=None):
        """
        Remove a member from the Quota Group (qgroup).

        Parameters:
        - host(str): the host (e.g. 'node1').
        - port(str): the port (e.g. '12345', default: '411').

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's set statistics, or an error msg if status !=0 or None
        """
        params = [ (LDMSD_Req_Attr.HOST, host),
                   (LDMSD_Req_Attr.PORT, port) ]
        return self.__comm_routine(LDMSD_Request.QGROUP_MEMBER_DEL, params)

    def qgroup_start(self):
        """
        Start the Quota Group (qgroup) service in the ldmsd.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's set statistics, or an error msg if status !=0 or None
        """
        return self.__comm_routine(LDMSD_Request.QGROUP_START)

    def qgroup_stop(self):
        """
        Stop the Quota Group (qgroup) service in the ldmsd.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's set statistics, or an error msg if status !=0 or None
        """
        return self.__comm_routine(LDMSD_Request.QGROUP_STOP)

    def qgroup_info(self):
        """
        Get the Quota Group (qgroup) information from the ldmsd.

        Returns:
        A tuple of status, data
        - status is an errno from the errno module
        - data is the daemon's set statistics, or an error msg if status !=0 or None
        """
        return self.__comm_routine(LDMSD_Request.QGROUP_INFO)

    def close(self):
        self.state = self.CLOSED
        if self.ldms:
            self.ldms.close()
            self.ldms = None

if __name__ == "__main__":
    comm = Communicator(
            "sock", "localhost", 10000, auth="munge"
        )
    error, status = comm.prdcr_add('orion-01', 'active', 'sock', 'orion-01', 10000, 20000000)
    error, status = comm.prdcr_status('orion-01')
    error, status = comm.smplr_status()
    error, status = comm.smplrset_status()
    pass

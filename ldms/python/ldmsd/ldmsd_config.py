#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2015 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2015 Sandia Corporation. All rights reserved.
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
"""
@module ldmsd_config

"""

import os
import socket


#:Dictionary contains the cmd_id, required attribute list
#:and optional attribute list of each ldmsd commands. For example,
#:LDMSD_CTRL_CMD_MAP['load']['id'] is the command id of the load command
#:LDMSD_CTRL_CMD_MAP['load']['req_attr'] is the list of the required attributes
#:of the load command.
#:LDMSD_CTRL_CMD_MAP['load']['opt_attr'] is the list of the optional attributes
#:of the load command.
LDMSD_CTRL_CMD_MAP = {'usage': {'id': 0, 'req_attr': []},
                      'load': {'id': 1,
                               'req_attr': ['name']},
                      'term': {'id': 2,
                               'req_attr': ['name']},
                      'config': {'id': 3,
                                 'req_attr': ['name', 'producer', 'instance']},
                      'start': {'id': 4,
                                'req_attr': ['name', 'interval'],
                                'opt_attr': ['offset']},
                      'stop': {'id': 5,
                               'req_attr': ['name']},
                      'add': {'id': 6,
                              'req_attr': ['host', 'type'],
                              'opt_attr': ['xprt', 'port', 'sets', 'interval',
                                      'offset', 'agg_no']},
                      'remove': {'id': 7,
                                 'req_attr': ['host']},
                      'store': {'id': 8,
                                'req_attr': ['name', 'policy', 'container', 'schema'],
                                'opt_attr': ['metric', 'hosts']},
                      'info': {'id': 9, 'req_attr': [],
                               'opt_attr': ['name']},
                      'udata': {'id': 10,
                                'req_attr': ['set', 'metric', 'udata']},
                      'exit': {'id': 11, 'req_attr': []},
                      'standby': {'id': 12,
                                  'req_attr': ['agg_no', 'state']},
                      'oneshot': {'id': 13,
                                  'req_attr': ['name', 'time']},
                      'udata_regex': {'id': 14,
                                      'req_attr': ['set', 'regex', 'base'],
                                      'opt_attr': ['incr']},
                      'version': {'id': 15, 'req_attr': [], 'opt_attr': []},
                      ###############################
                      # LDMSD command version 2
                      ###############################
                      ##### Producer Policy #####
                      'prdcr_add': {'id': 20,
                                    'req_attr': ['name', 'type', 'xprt', 'host',
                                             'port', 'interval']},
                      'prdcr_del': {'id': 21,
                                    'req_attr': ['name']},
                      'prdcr_start': {'id': 22,
                                      'req_attr': ['name'],
                                      'opt_attr': ['interval']},
                      'prdcr_stop': {'id': 23,
                                     'req_attr': ['name']},
                      'prdcr_start_regex': {'id': 24,
                                            'req_attr': ['regex'],
                                            'opt_attr': ['interval']},
                      'prdcr_stop_regex': {'id': 25,
                                           'req_attr': ['regex']},
                      ##### Updater Policy #####
                      'updtr_add': {'id': 30,
                                     'req_attr': ['name', 'interval'],
                                     'opt_attr': ['offset']},
                      'updtr_del': {'id': 31,
                                     'req_attr': ['name']},
                      'updtr_match_add': {'id': 32,
                                          'req_attr': ['name', 'regex', 'match']},
                      'updtr_match_del': {'id': 33,
                                          'req_attr': ['name', 'regex', 'match']},
                      'updtr_prdcr_add': {'id': 34,
                                          'req_attr': ['name', 'regex']},
                      'updtr_prdcr_del': {'id': 35,
                                          'req_attr': ['name', 'regex']},
                      'updtr_start': {'id': 38,
                                      'req_attr': ['name'],
                                      'opt_attr': ['interval', 'offset']},
                      'updtr_stop': {'id': 39,
                                     'req_attr': ['name']},
                      ##### Storage Policy #####
                      'strgp_add': {'id': 40,
                                     'req_attr': ['name', 'plugin', 'container',
                                              'schema'],
                                    'opt_attr': ['rotate']},
                      'strgp_del': {'id': 41,
                                    'req_attr': ['name']},
                      'strgp_prdcr_add': {'id': 42,
                                          'req_attr': ['name', 'regex']},
                      'strgp_prdcr_del': {'id': 43,
                                          'req_attr': ['name', 'regex']},
                      'strgp_metric_add': {'id': 44,
                                           'req_attr': ['name', 'metric']},
                      'strgp_metric_del': {'id': 45,
                                           'req_attr': ['name', 'metric']},
                      'strgp_start': {'id': 48,
                                      'req_attr': ['name']},
                      'strgp_stop': {'id': 49,
                                     'req_attr': ['name']},
                      }

"""@var MAX_RECV_LEN
  The maximum length of the message received back from ldmsd. The default is 4096.
"""
MAX_RECV_LEN = 4096

PYTHON_SOCK = "/home/nichamon/suitcase/var/run/ovis/ldmsd-test-python.sock"
LDMSD_SOCK = "/home/nichamon/suitcase/var/run/ovis/ldmsd.sock"

class ldmsdConfig(object):
    __metaclass__ = ABCMeta

    def socket(self):
        """socket attribute of the object
        """
        raise NotImplementedError

    @abstractmethod
    def send_command(self, cmd):
        """Send the string of a command to an ldmsd process
        """

    @abstractmethod
    def receive_response(self):
        """Receive a response from the ldmsd process
        """
        data_all = ""
        data = self.socket.recv(self.max_recv_len)
        while '\x00' not in data:
            data_all += data
            data = self.socket.recv(self.max_recv_len)
        data_all += data
        data = data_all.split('\x00')[0]
        return data

    @abstractmethod
    def close(self):
        """Close the socket
        """
        self.socket.close()

    def get_cmd_id(self, cmd_verb):
        """Return the command ID of the given command
        """
        return LDMSD_CTRL_CMD_MAP[cmd_verb]['id']

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

    def talk(self, cmd):
        """Send command and receive response to and from ldmsd
        """
        self.send_command(cmd + "\0")
        return self.receive_response()

    def __format_cmd(self, cmd_key, attr_value_dict):
        cmd_id = LDMSD_CTRL_CMD_MAP[cmd_key]['id']
        s = "{0}{1}".format(cmd_id, cmd_key)
        if attr_value_dict is None:
            return s

        for attr in attr_value_dict:
            if attr_value_dict[attr] is not None:
                s += " {0}={1}".format(attr, attr_value_dict[attr])
        return s

    def load(self, name):
        attr_value_dict = {'name': name}
        cmd = self.__format_cmd('load', attr_value_dict)
        return self.talk(cmd)

    def usage(self):
        return self.talk(self.__format_cmd('usage', {}))

    def term(self, name):
        return self.talk(self.__format_cmd('term', {'name': name}))

    def config(self, name, **kwargs):
        kwargs.update({'name': name})
        cmd = self.__format_cmd('config', kwargs)
        return self.talk(cmd)

    def start(self, name, interval):
        cmd = self.__format_cmd('start', {'name': name,
                                                  'interval': interval})
        return self.talk(cmd)

    def stop(self, name):
        cmd = self.__format_cmd('stop', {'name': name})
        return self.talk(cmd)

    def add(self, host, host_type, xprt = None, port = None,
                     interval = None, sets = None,
                     offset = None, standby = None):
        attr_values = {'host': host, 'type': host_type, 'xprt': xprt, 'port': port,
                       'sets': sets}
        if interval:
            attr_values['interval'] = interval

        if offset:
            attr_values['offset'] = offset

        if standby:
            attr_values['standby'] = standby
        return self.talk(self.__format_cmd('add', attr_values))

    def store(self, store_pi, policy, container, schema, metrics = None, hosts = None):
        attr_values = {'name': store_pi, 'policy': policy, 'container': container,
                       'schema': schema}

        if metrics:
            attr_values['metrics'] = metrics

        if hosts:
            attr_values['hosts'] = hosts
        return self.talk(self.__format_cmd('store', attr_values))

    def info(self, name = None):
        attr_values = {}
        if name:
            attr_values['name'] = name
        return self.talk(self.__format_cmd('info', attr_values))

    def set_udata(self, set, metric, udata):
        cmd = self.__format_cmd('udata', {'set': set, 'metric': metric,
                                              'udata': udata})
        return self.talk(cmd)

    def set_udata_regex(self, set, regex, base, incr = None):
        cmd = self.__format_cmd('udata_regex', {'set': set,
                                                'regex': regex,
                                                'base': base,
                                                'incr': incr})
        return self.talk(cmd)

    def exit_daemon(self):
        return self.talk(self.__format_cmd('exit', {}))

    def update_standby(self, aggregator_id, state):
        attr_values = {'agg_no': aggregator_id,
                       'state': state}
        return self.talk(self.__format_cmd('standby', attr_values))

    def oneshot(self, sampler_name, time):
        attr_values = {'name': sampler_name, 'time': time}
        return self.talk(self.__format_cmd('oneshot', attr_values))

    def version(self):
        return self.talk(self.__format_cmd('version', {}))

    #############################################
    # LDMSD command version 2
    #############################################

    def prdcr_add(self, name, xprt, host, port, type, interval):
        attr_values = {'name': name,
                       'xprt': xprt,
                       'host': host,
                       'port': port,
                       'type': type,
                       'interval': interval}
        return self.talk(self.__format_cmd('prdcr_add', attr_values))

    def prdcr_del(self, name):
        return self.talk(self.__format_cmd('prdcr_del', {'name': name}))

    def prdcr_start(self, name, interval = None):
        attr_values = {'name': name, 'interval': interval}
        return self.talk(self.__format_cmd('prdcr_start', attr_values))

    def prdcr_start_regex(self, regex, interval = None):
        attr_values = {'regex': regex, 'interval': interval}
        return self.talk(self.__format_cmd('prdcr_start_regex', attr_values))

    def prdcr_stop(self, name):
        return self.talk(self.__format_cmd('prdcr_stop', {'name': name}))

    def prdcr_stop_regex(self, regex):
        return self.talk(self.__format_cmd('prdcr_stop_regex', {'regex': regex}))

    def updtr_add(self, name, interval, offset = None):
        attr_values = {'name': name, 'interval': interval, 'offset': offset}
        return self.talk(self.__format_cmd('updtr_add', attr_values))

    def updtr_del(self, name):
        return self.talk(self.__format_cmd('updtr_del', {'name': name}))

    def updtr_start(self, name, interval = None, offset = None):
        attr_values = {'name': name, 'interval': interval, 'offset': offset}
        return self.talk(self.__format_cmd('updtr_start', attr_values))

    def updtr_stop(self, name):
        return self.talk(self.__format_cmd('updtr_stop', {'name': name}))

    def updtr_match_add(self, name, match, regex):
        attr_values = {'name': name, 'match': match, 'regex': regex}
        return self.talk(self.__format_cmd('updtr_match_add', attr_values))

    def updtr_match_del(self, name, match, regex):
        attr_values = {'name': name, 'match': match, 'regex': regex}
        return self.talk(self.__format_cmd('updtr_match_del', attr_values))

    def updtr_prdcr_add(self, name, regex):
        attr_values = {'name': name, 'regex': regex}
        return self.talk(self.__format_cmd('updtr_prdcr_add', attr_values))

    def updtr_prdcr_del(self, name, regex):
        attr_values = {'name': name, 'regex': regex}
        return self.talk(self.__format_cmd('updtr_prdcr_del', attr_values))

    def strgp_add(self, name, plugin, container, schema):
        attr_values = {'name': name, 'plugin': plugin,
                       'container': container, 'schema': schema}
        return self.talk(self.__format_cmd('strgp_add', attr_values))

    def strgp_del(self, name):
        return self.talk(self.__format_cmd('strgp_del', {'name': name}))

    def strgp_prdcr_add(self, name, regex):
        attr_values = {'name': name, 'regex': regex}
        return self.talk(self.__format_cmd('strgp_prdcr_add', attr_values))

    def strgp_prdcr_del(self, name, regex):
        attr_values = {'name': name, 'regex': regex}
        return self.talk(self.__format_cmd('strgp_prdcr_add', attr_values))

    def strgp_metric_add(self, name, metric):
        attr_values = {'name': name, 'metric': metric}
        return self.talk(self.__format_cmd('strgp_metric_add', attr_values))

    def strgp_metric_del(self, name, metric):
        attr_values = {'name': name, 'metric': metric}
        return self.talk(self.__format_cmd('strgp_metric_del', attr_values))

    def strgp_start(self, name):
        return self.talk(self.__format_cmd('strgp_start', {'name': name}))

    def strgp_stop(self, name):
        return self.talk(self.__format_cmd('strgp_stop', {'name': name}))

class ldmsdUSocketConfig(ldmsdConfig):
    def __init__(self, ldmsd_sockpath, sockpath = None, max_recv_len = None):
        if not os.path.exists(ldmsd_sockpath):
            self.socket = None
            self.sockpath = None
            raise ValueError("{0} doesn't exist.".format(ldmsd_sockpath))

        if sockpath is None:
            name = basename(ldmsd_sockpath)
            tmp = name.split(".")
            name = "{0}_ctrl_{1}.{2}".format(tmp[0], os.getpid(), tmp[1])
            self.sockpath = dirname(ldmsd_sockpath) + "/" + name
        else:
            self.sockpath = sockpath
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.socket.bind(self.sockpath)

        if max_recv_len is None:
            self.max_recv_len = MAX_RECV_LEN
        else:
            self.max_recv_len = max_recv_len
        self.ldmsd_sockpath = ldmsd_sockpath

    def __del__(self):
        if self.socket is not None:
            self.socket.close()
        if self.sockpath is not None:
            os.unlink(self.sockpath)

    def setMaxRecvLen(self, max_recv_len):
        self.max_recv_len = max_recv_len

    def getMaxRecvLen(self):
        return self.max_recv_len

    def getSockPath(self):
        return self.sockpath

    def setLdmsdSockPath(self, ldmsd_sockpath):
        self.ldmsd_sockpath = ldmsd_sockpath

    def getLdmsdSockPath(self):
        return self.ldmsd_sockpath

    def send_command(self, cmd):
        cmd_len = self.socket.sendto(cmd, self.ldmsd_sockpath)
        if cmd_len != len(cmd):
            raise Exception("Wrong command length")

    def receive_response(self):
        return ldmsdConfig.receive_response(self)

    def close(self):
        self.socket.close()
        os.unlink(self.sockpath)

class ldmsdInetConfig(ldmsdConfig):
    def __init__(self, host, port, max_recv_len = MAX_RECV_LEN):
        self.socket = None
        self.host = host
        self.port = port
        self.max_recv_len = max_recv_len
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((host, port))

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
        self.socket.sendall(cmd)

    def receive_response(self):
        return ldmsdConfig.receive_response(self)

    def close(self):
        self.socket.close()

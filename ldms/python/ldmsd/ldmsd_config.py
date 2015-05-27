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
'''
Created on Apr 28, 2015

@author: nichamon
'''
from abc import ABCMeta, abstractmethod
from os.path import basename, dirname
"""
@module ldmsd_test_ctrl

"""

import os
import socket

LDMSCTL_CMD_ID_MAP = {'list_pi': {'id': 0, 'verb': "usage"},
                      'load_pi': {'id': 1, 'verb': "load"},
                      'term_pi': {'id': 2, 'verb': "term"},
                      'cfg_pi': {'id': 3, 'verb': "config"},
                      'start_sampler': {'id': 4, 'verb': "start"},
                      'stop_sampler': {'id': 5, 'verb': "stop"},
                      'add_host': {'id': 6, 'verb': "add"},
                      'rem_host': {'id': 7, 'verb': "remove"},
                      'store': {'id': 8, 'verb': "store"},
                      'info_daemon': {'id': 9, 'verb': "info"},
                      'set_udata': {'id': 10, 'verb': "udata"},
                      'exit_daemon': {'id': 11, 'verb': "exit"},
                      'update_standby': {'id': 12, 'verb': "standby"},
                      'oneshot_sampler': {'id': 13, 'verb': "oneshot"}}

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

    def talk(self, cmd):
        self.send_command(cmd + "\0")
        return self.receive_response()

    def __format_cmd(self, cmd_key, attr_value_dict):
        cmd = LDMSCTL_CMD_ID_MAP[cmd_key]
        s = "{0}{1}".format(cmd['id'], cmd['verb'])
        if attr_value_dict is None:
            return s

        for attr in attr_value_dict:
            s += " {0}={1}".format(attr, attr_value_dict[attr])
        return s

    def load(self, name):
        attr_value_dict = {'name': name}
        cmd = self.__format_cmd('load_pi', attr_value_dict)
        return self.talk(cmd)

    def usage(self):
        return self.talk(self.__format_cmd('list_pi', {}))

    def term(self, name):
        return self.talk(self.__format_cmd('term_pi', {'name': name}))

    def config(self, name, **kwargs):
        kwargs.update({'name': name})
        cmd = self.__format_cmd('cfg_pi', kwargs)
        return self.talk(cmd)

    def start(self, name, interval):
        cmd = self.__format_cmd('start_sampler', {'name': name,
                                                  'interval': interval})
        return self.talk(cmd)

    def stop(self, name):
        cmd = self.__format_cmd('stop_sampler', {'name': name})
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
        return self.talk(self.__format_cmd('add_host', attr_values))

    def store(self, store_pi, policy, container, schema, metrics = None, hosts = None):
        attr_values = {'name': store_pi, 'policy': policy, 'container': container,
                       'schema': schema}

        if metrics:
            attr_values['metrics'] = metrics

        if hosts:
            attr_values['hosts'] = hosts
        return self.talk(self.__format_cmd('store', attr_values))

    def info(self):
        return self.talk(self.__format_cmd('info_daemon', {}))

    def set_udata(self, set, metric, udata):
        cmd = self.__format_cmd('set_udata', {'set': set, 'metric': metric,
                                              'udata': udata})
        return self.talk(cmd)

    def exit_daemon(self):
        return self.talk(self.__format_cmd('exit_daemon', {}))

    def update_standby(self, aggregator_id, state):
        attr_values = {'agg_no': aggregator_id,
                       'state': state}
        return self.talk(self.__format_cmd('update_standby', attr_values))

    def oneshot(self, sampler_name, time):
        attr_values = {'name': sampler_name, 'time': time}
        return self.talk(self.__format_cmd('oneshot', attr_values))

class ldmsdUSocketConfig(ldmsdConfig):
    def __init__(self, ldmsd_sockpath, sockpath = None, max_recv_len = None):
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
        self.socket.close()
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
        self.host = host
        self.port = port
        self.max_recv_len = max_recv_len
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((host, port))

    def __del__(self):
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

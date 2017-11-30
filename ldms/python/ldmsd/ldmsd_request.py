#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2016-2017 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2016-2017 Sandia Corporation. All rights reserved.
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
import struct
import cmd
import json
import argparse
import sys
import traceback

class LDMSD_Except(Exception):
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)

class LDMSD_Req_Attr(object):
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
    TEST = 24
    LAST = 26

    NAME_ID_MAP = {'name': NAME,
                   'interval': INTERVAL,
                   'interval_us': INTERVAL,
                   'offset': OFFSET,
                   'regex': REGEX,
                   'type': TYPE,
                   'producer': PRODUCER,
                   'instance': INSTANCE,
                   'xprt': XPRT,
                   'host': HOST,
                   'port': PORT,
                   'match': MATCH,
                   'plugin': PLUGIN,
                   'container': CONTAINER,
                   'schema': SCHEMA,
                   'metric': METRIC,
                   'udata': UDATA,
                   'base': BASE,
                   'incr': INCREMENT,
                   'level': LEVEL,
                   'path': PATH,
                   'time': TIME,
                   'test': TEST,
        }

    def __init__(self, value = None, attr_name = None, attr_id = None):
        if attr_id:
            self.attr_id = attr_id
        else:
            if attr_name:
                try:
                    self.attr_id = self.NAME_ID_MAP[attr_name]
                except KeyError:
                    raise
            else:
                # Assume this is the last attribute.
                self.attr_id = self.LAST

        if self.attr_id == self.LAST:
            self.packed = struct.pack("i", 0)
        else:
            self.attr_value = value
            if value is None:
                self.attr_len = 0
                self.fmt = 'iiq'
                self.packed = struct.pack(self.fmt, 1, self.attr_id,
                                                    self.attr_len)
            else:
                # One is added to account for the terminating zero
                self.attr_len = int(len(value)+1)
                self.fmt = 'iiq' + str(self.attr_len) + 's'
                self.packed = struct.pack(self.fmt, 1, self.attr_id,
                                          self.attr_len, self.attr_value)

    def __len__(self):
        return len(self.packed)

    def pack(self):
        return self.packed

class LDMSD_Request(object):
    EXAMPLE = 1
    GREETING = 2

    PRDCR_ADD = 0x100
    PRDCR_DEL = 0x100 + 1
    PRDCR_START = 0x100 + 2
    PRDCR_STOP = 0x100 + 3
    PRDCR_STATUS = 0x100 + 4
    PRDCR_START_REGEX = 0x100 + 5
    PRDCR_STOP_REGEX = 0x100 + 6
    PRDCR_SET_STATUS = 0x100 + 7

    STRGP_ADD = 0x200
    STRGP_DEL = 0x200 + 1
    STRGP_START = 0x200 + 2
    STRGP_STOP = 0x200 + 3
    STRGP_STATUS = 0x200 + 4
    STRGP_PRDCR_ADD = 0X200 + 5
    STRGP_PRDCR_DEL = 0X200 + 6
    STRGP_METRIC_ADD = 0X200 + 7
    STRGP_METRIC_DEL = 0X200 + 8

    UPDTR_ADD = 0X300
    UPDTR_DEL = 0X300 + 1
    UPDTR_START = 0X300 + 2
    UPDTR_STOP = 0X300 + 3
    UPDTR_STATUS = 0x300 + 4
    UPDTR_PRDCR_ADD = 0X300 + 5
    UPDTR_PRDCR_DEL = 0X300 + 6
    UPDTR_MATCH_ADD = 0X300 + 7
    UPDTR_MATCH_DEL = 0X300 + 8

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

    LDMSD_REQ_ID_MAP = {
            'example': {'id': EXAMPLE},
            'greeting': {'id': GREETING},

            'prdcr_add': {'id': PRDCR_ADD},
            'prdcr_del': {'id': PRDCR_DEL},
            'prdcr_start': {'id': PRDCR_START},
            'prdcr_stop': {'id': PRDCR_STOP},
            'prdcr_status': {'id': PRDCR_STATUS},
            'prdcr_start': {'id': PRDCR_START},
            'prdcr_start_regex': {'id': PRDCR_START_REGEX},
            'prdcr_stop': {'id': PRDCR_STOP},
            'prdcr_stop_regex': {'id': PRDCR_STOP_REGEX},
            'prdcr_set_status': {'id': PRDCR_SET_STATUS},

            'strgp_add': {'id': STRGP_ADD},
            'strgp_del': {'id': STRGP_DEL},
            'strgp_start': {'id': STRGP_START},
            'strgp_stop': {'id': STRGP_STOP},
            'strgp_status': {'id': STRGP_STATUS},
            'strgp_prdcr_add': {'id': STRGP_PRDCR_ADD},
            'strgp_prdcr_del': {'id': STRGP_PRDCR_DEL},
            'strgp_metric_add': {'id': STRGP_METRIC_ADD},
            'strgp_metric_del': {'id': STRGP_METRIC_DEL},

            'updtr_add': {'id': UPDTR_ADD},
            'updtr_del': {'id': UPDTR_DEL},
            'updtr_start': {'id': UPDTR_START},
            'updtr_stop': {'id': UPDTR_STOP},
            'updtr_status': {'id': UPDTR_STATUS},
            'updtr_prdcr_add': {'id': UPDTR_PRDCR_ADD},
            'updtr_prdcr_del': {'id': UPDTR_PRDCR_DEL},
            'updtr_match_add': {'id': UPDTR_MATCH_ADD},
            'updtr_match_del': {'id': UPDTR_MATCH_DEL},

            'start': {'id': PLUGN_START},
            'stop': {'id': PLUGN_STOP},
            'plugn_status': {'id': PLUGN_STATUS},
            'load': {'id': PLUGN_LOAD},
            'term': {'id': PLUGN_TERM},
            'config': {'id': PLUGN_CONFIG},
            'usage': {'id': PLUGN_LIST},

            'udata': {'id': SET_UDATA},
            'udata_regex': {'id': SET_UDATA_REGEX},
            'loglevel': {'id': VERBOSITY_CHANGE},
            'daemon_status': {'id': DAEMON_STATUS},
            'version': {'id': VERSION},
            'env': {'id': ENV},
            'include': {'id': INCLUDE},
            'oneshot': {'id': ONESHOT},
            'logrotate': {'id': LOGROTATE},
            'daemon_exit': {'id': EXIT_DAEMON},
        }

    TYPE_CONFIG_CMD = 1
    TYPE_CONFIG_RESP = 2
    TYPE_LAST = 3

    SOM_FLAG = 1
    EOM_FLAG = 2
    message_number = 1
    header_size = 24
    def __init__(self, command=None, command_id=None, message=None, attrs=None):
        marker = -1
        if command_id is None and command is None:
            raise Exception("Need either command or command_id")
        if command_id is None:
            if command not in self.LDMSD_REQ_ID_MAP.keys():
                raise KeyError("Command '{0}' is not supported.".format(command))
            command_id = self.LDMSD_REQ_ID_MAP[command]['id']

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

        self.request = struct.pack('iiiiii', marker, self.TYPE_CONFIG_CMD,
                                   LDMSD_Request.SOM_FLAG | LDMSD_Request.EOM_FLAG,
                                   self.message_number, command_id, self.request_size)
        # Add the attributes after the message header
        if attrs:
            for attr in attrs:
                self.request += attr.pack()
            self.request += struct.pack('i', 0) # terminate list
        # Add any message payload
        if message:
            self.request += message
        self.response = {'errcode': None, 'msg': None}
        LDMSD_Request.message_number += 1

    def message_number_get(self):
        return self.message_number

    def send(self, ctrl):
        try:
            ctrl.send_command(bytes(self.request))
        except:
            raise

    def receive(self, ctrl):
        self.response = None
        msg = None
        while True:
            record = ctrl.receive_response()
            (marker, msg_type, msg_flags, msg_no,
             errcode, rec_len) = struct.unpack('iiiiii',
                                               record[:self.header_size])
            if marker != -1:
                raise ValueError("Record is missing the marker")
            data = record[self.header_size:]
            if msg is None:
                msg = data
            else:
                msg += data
            if (msg_flags & LDMSD_Request.EOM_FLAG) != 0:
                return { 'errcode' : errcode, 'msg' : str(msg) }
                return True
        # unreachable

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

class LdmsdReqParser(cmd.Cmd):
    def __init__(self, host = None, port = None, secretPath = None, infile=None):
        try:
            self.secretword = None
            if secretPath is not None:
                try:
                    from ovis_lib import ovis_auth
                except ImportError:
                    raise ImportError("No module ovis_lib. Please make sure that ovis"
                                        "is built with --enable-swig")
                self.secretword = ovis_auth.ovis_auth_get_secretword(secretPath, None)

                self.ctrl = ldmsd_config.ldmsdInetConfig(host = host,
                                                         port = int(port),
                                                         secretword = self.secretword)
                self.prompt = "{0}:{1}> ".format(host, port)

            if infile:
                cmd.Cmd.__init__(self, stdin=infile)
            else:
                cmd.Cmd.__init__(self)
        except:
            raise

    def do_quit(self, arg):
        """
        Quit the ldmsd_request interface
        """
        self.ctrl.close()
        return True

    def complete_prdcr_set(self, text, line, begidx, endidx):
        attr_list = ['prdcr']
        return ["{0}=".format(attr) for attr in attr_list if attr.startswith(text)]


    def do_prdcr_set(self, arg):
        """
        Print the list of producer sets of a producer and their status
        """
        try:
            prdcr_name = LDMSD_Req_Attr(LDMSD_Req_Attr.PRODUCER, arg.split("=")[1])
            req = LDMSD_Request(LDMSD_Request.PRDCR_METRIC_SET, attrs = [prdcr_name,])
            req.send(self.ctrl.socket)
            metric_sets = req.receive(self.ctrl.socket)
            metric_sets = json.loads(metric_sets)
            if req.is_error_resp(metric_sets):
                print("Error: {0}".format(metric_sets[0]['error']))
            else:
                print("Name             Schema Name      State")
                print("---------------- ---------------- ------------ ")
                for pset in metric_sets:
                    print("    {0:16} {1:16} {2}".format(pset['inst_name'],
                                                         pset['schema_name'],
                                                         pset['state']))
        except:
            raise

    def do_prdcr_add(self, arg):
        """
        Add a producer.
        """
        pass

if __name__ == "__main__":
    is_debug = True
    try:
        parser = argparse.ArgumentParser(description="Configure an LDMS Daemon.")
        parser.add_argument("--host", help = "Hostname of ldmsd to connect to")
        parser.add_argument('--port',
                            help = "Inet ctrl listener port of ldmsd")
        parser.add_argument('--auth_file',
                            help = "Path to the file containing the secretword. \
This must be use only when the ldmsd is using authentication and \
ldmsd_controller is not connecting to ldmsd through a unix domain socket.")
        parser.add_argument('--debug', action = "store_true",
                            help = argparse.SUPPRESS)
        args = parser.parse_args()
        is_debug = args.debug

        if args.host is None or args.port is None:
            print("Please give --host and --port")
            sys.exit(1)

        reqParser = LdmsdReqParser(host = args.host, port = args.port,
                                        secretPath = args.auth_file)

        reqParser.cmdloop("Welcome to the LDMSD control processor")

    except KeyboardInterrupt:
        sys.exit(0)
    except Exception as e:
        if is_debug:
            print(is_debug)
            traceback.print_exc()
            sys.exit(2)
        else:
            print(e)

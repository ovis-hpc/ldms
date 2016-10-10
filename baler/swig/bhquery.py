#!/usr/bin/env python

#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2015-2016 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2015-2016 Sandia Corporation. All rights reserved.
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


"""bhttpd query interface

This module is an interface to bhttpd/query web service.

"""

import httplib
import urllib
import json
import heapq
import logging
import StringIO
import os
import re
import struct
from datetime import datetime, date

DEFAULT_LOG_LEVEL = logging.WARNING

logger = logging.getLogger(__name__)
logger.setLevel(DEFAULT_LOG_LEVEL)

logch = logging.StreamHandler()
logch.setLevel(DEFAULT_LOG_LEVEL)

logformat = logging.Formatter('%(asctime)s %(name)s %(levelname)s: %(message)s')

logch.setFormatter(logformat)

logger.addHandler(logch)

_QUERY = "/query"
_SERVERS = set()
#_SERVERS = {"localhost:18888", "localhost:18889"}

def add_servers(servers):
    """Add servers into the module's server list.

    Args:
        servers (list): The list of servers (strings) to add.

    Returns:
        None
    """
    global _SERVERS
    for ss in servers:
        for s in ss.split(','):
            _SERVERS.add(s)

def add_servers_from_env():
    servers = [ os.environ['BHTTPD_SERVERS'] ]
    add_servers(servers)

add_servers_from_env()


def rm_servers(servers):
    """Remove servers from the module's server list.

    Args:
        servers (list): The list of servers (strings) to remove.

    Returns:
        None
    """
    global _SERVERS
    for ss in servers:
        for s in ss.split(','):
            try:
                _SERVERS.remove(s)
            except:
                pass

class BHTTPDConn(object):
    def __init__(self, server):
        logger.info("connecting to bhttpd: %s", server)
        self.conn = httplib.HTTPConnection(server)
        self.server = server
        self.resp = None

    def __del__(self):
        logger.info("closing connection to bhttpd: %s", self.server)
        self.conn.close()

    def req(self, path, params = None):
        uri = path
        if params:
            _p = urllib.urlencode({k: params[k] for k in params if params[k] != None})
            uri = uri + "?" + _p
        logger.debug("requesting server: %s, uri: %s", self.server, uri)
        self.conn.request("GET", uri)
        resp = self.conn.getresponse()
        data = resp.read()
        logger.debug("---- data ----")
        logger.debug(data)
        logger.debug("--------------")
        return data

    def get(self, path, params = None):
        uri = path
        if params:
            _p = urllib.urlencode({k: params[k] for k in params if params[k] != None})
            uri = uri + "?" + _p
        logger.debug("requesting server: %s, uri: %s", self.server, uri)
        self.conn.request("GET", uri)
        self.resp = self.conn.getresponse()

    def fetch(self):
        logger.debug("fetching ...")
        if not self.resp:
            logger.debug("no response")
            return []
        if self.resp.status != 200:
            raise Exception("Server error: " + str(self.resp.status) + self.resp.reason)
        data = self.resp.read(1024)
        logger.debug("data length: %d", len(data))
        return data

def ts2str(ts):
    dt = datetime.fromtimestamp(ts)
    return dt.strftime('%Y-%m-%d %H:%M:%S.%f')

def print_ptn_header():
    print '  '.join([
        '------',
        '----------------',
        '---------------------------',
        '---------------------------',
        '------'
    ])
    print '  '.join([
        '%6s' % ('ptn_id'),
        '%16s' % ('count'),
        '%27s' % ('first seen'),
        '%27s' % ('last seen'),
        ('pattern')
    ])
    print '  '.join([
        '------',
        '----------------',
        '---------------------------',
        '---------------------------',
        '------'
    ])

def print_ptn_footer():
    print '  '.join([
        '------',
        '----------------',
        '---------------------------',
        '---------------------------',
        '------'
    ])

def print_ptn(ptn):
    # ptn_id count first-seen last-seen pattern
    print '  '.join(['%6d' % (ptn['ptn_id']),
        '%16d' % (ptn['count']),
        '%27s' % (ts2str(ptn['first_seen'])),
        '%27s' % (ts2str(ptn['last_seen'])),
        ''.join(s['text'] for s in ptn['msg'])
        ])

def get_ptns():
    """Get patterns from all servers.

    Args:
        None

    Returns:
        Pattern objects.
    """
    global _SERVERS
    uptns = {} # unique patterns
    for s in _SERVERS:
        conn = BHTTPDConn(s)
        ret = conn.req("/query", {"type": "ptn", "use_ts_fmt": "1"})
        del conn
        j = json.loads(ret)
        j = j['result']
        for x in j:
            ptn_id = x['ptn_id']
            y = None
            try:
                y = uptns[ptn_id]
            except KeyError:
                pass
            if y:
                # merge x into y
                x_first_seen = x['first_seen']
                x_last_seen = x['last_seen']
                if x_first_seen and x_first_seen < y['first_seen']:
                    y['first_seen'] = x_first_seen
                if x_last_seen and x_last_seen > y['last_seen']:
                    y['last_seen'] = x_last_seen
                y['count'] += x['count']
            else:
                uptns[x['ptn_id']] = x

    return uptns.values()

class MsgHeapEntry(object):
    def __init__(self, msg, index):
        [self.ts, self.host, self.msg] = msg.split(' ',2)
        self.index = index

    def __lt__(self, other):
        if self.ts < other.ts:
            return True
        if self.ts > other.ts:
            return False
        if self.host < other.host:
            return True
        return False

    def __le__(self, other):
        if self.ts < other.ts:
            return True
        if self.ts > other.ts:
            return False
        if self.host <= other.host:
            return True
        return False

    def __gt__(self, other):
        if self.ts > other.ts:
            return True
        if self.ts < other.ts:
            return False
        if self.host > other.host:
            return True
        return False

    def __ge__(self, other):
        if self.ts > other.ts:
            return True
        if self.ts < other.ts:
            return False
        if self.host >= other.host:
            return True
        return False

    def __eq__(self, other):
        return self.ts == other.ts and self.host == other.host

    def __ne__(self, other):
        return self.ts != other.ts or self.host != other.host
# end of MsgHeapEntry

class PxlHeapEntry(object):
    def __init__(self, pxl, index):
        self.pxl = pxl
        self.index = index

    def __lt__(self, other):
        if self.pxl.ptn_id < other.pxl.ptn_id:
            return True
        if self.pxl.ptn_id > other.pxl.ptn_id:
            return False
        if self.pxl.sec < other.pxl.sec:
            return True
        if self.pxl.sec > other.pxl.sec:
            return False
        if self.pxl.comp_id < other.pxl.comp_id:
            return True
        if self.pxl.comp_id > other.pxl.comp_id:
            return False
        return False

    def __le__(self, other):
        if self.pxl.ptn_id < other.pxl.ptn_id:
            return True
        if self.pxl.ptn_id > other.pxl.ptn_id:
            return False
        if self.pxl.sec < other.pxl.sec:
            return True
        if self.pxl.sec > other.pxl.sec:
            return False
        if self.pxl.comp_id < other.pxl.comp_id:
            return True
        if self.pxl.comp_id > other.pxl.comp_id:
            return False
        return True

    def __gt__(self, other):
        if self.pxl.ptn_id > other.pxl.ptn_id:
            return True
        if self.pxl.ptn_id < other.pxl.ptn_id:
            return False
        if self.pxl.sec > other.pxl.sec:
            return True
        if self.pxl.sec < other.pxl.sec:
            return False
        if self.pxl.comp_id > other.pxl.comp_id:
            return True
        if self.pxl.comp_id < other.pxl.comp_id:
            return False
        return False

    def __ge__(self, other):
        if self.pxl.ptn_id > other.pxl.ptn_id:
            return True
        if self.pxl.ptn_id < other.pxl.ptn_id:
            return False
        if self.pxl.sec > other.pxl.sec:
            return True
        if self.pxl.sec < other.pxl.sec:
            return False
        if self.pxl.comp_id > other.pxl.comp_id:
            return True
        if self.pxl.comp_id < other.pxl.comp_id:
            return False
        return True

    def __eq__(self, other):
        if self.pxl.ptn_id != other.pxl.ptn_id:
            return False
        if self.pxl.sec != other.pxl.sec:
            return False
        if self.pxl.comp_id != other.pxl.comp_id:
            return False
        return True

    def __ne__(self, other):
        if self.pxl.ptn_id != other.pxl.ptn_id:
            return True
        if self.pxl.sec != other.pxl.sec:
            return True
        if self.pxl.comp_id != other.pxl.comp_id:
            return True
        return False

# end of PxlHeapEntry

class Msg(object):
    """Message wrapper, wrapping JSON msg returned from balerd server"""
    def __init__(self, msg):
        self.msg = msg

    def __str__(self):
        return ' '.join([str(x) for x in [self.msg.ts, self.msg.host, self.msg.msg]])

class Pixel(object):
    """Pixel wrapper"""
    def __init__(self, sec, comp_id, ptn_id, count):
        self.sec = sec
        self.comp_id = comp_id
        self.ptn_id = ptn_id
        self.count = count

    def __str__(self):
        tmp = [str(x) for x in [self.ptn_id, self.sec, self.comp_id, self.count]]
        return ', '.join(tmp)

def byte_str_to_pixel(s):
    data = struct.unpack('!IIII', s)
    p = Pixel(data[0], data[1], data[2], data[3])
    return p

def ts_fmt_check(ts):
    if not ts:
        return True;
    m = re.match('^\d\d\d\d-\d\d-\d\d \d\d:\d\d:\d\d$', ts)
    if m:
        return True
    m = re.match('^\d+$', ts)
    if m:
        return True
    return False

class ImgQuery(object):
    """Image data query utility.

    ImgQuery object is a utility to help query image data across bhttpds.

    Example:
        >>> iq = query.ImgQuery(host_ids = "1-10,20", ptn_ids="128-130,155",
        >>>                     ts0="1442943270", ts1="1442943270",
        >>>                     img_store="3600-1" )
        >>> for pixel in iq:
        >>>     print pixel

    """
    def __init__(self, img_store = None, host_ids = None,
                 ptn_ids = None, ts0 = None, ts1 = None):
        global _SERVERS;

        if not img_store:
            raise Exception("img_store is needed")
        self.server = []
        self.heap = []
        self.conn = []
        self.params = {
            "type": "img",
            "host_ids": host_ids,
            "ptn_ids": ptn_ids,
            "ts0": ts0,
            "ts1": ts1,
            "img_store": img_store,
        }
        self.pxlbuff = []

        self.N = len(_SERVERS)
        if not ts_fmt_check(ts0) or not ts_fmt_check(ts1):
            raise Exception("Unsupported timestamp format. Only '%Y-%m-%d %H:%M:%S' and 'seconds_since_epoch' are supported.");

        for s in _SERVERS:
            self.server.append(s)
            self.conn.append(BHTTPDConn(s))

        for i in range(self.N):
            self.pxlbuff.append(None)
            self.conn[i].get("/query", self.params)
            self._fetch_pixels(i)
            pxl = self._get_pixel(i)
            if not pxl:
                continue
            hent = PxlHeapEntry(pxl, i)
            self.heap.append(hent)

        heapq.heapify(self.heap)

    def __del__(self):
        logger.debug("destroying ImgQuery")
        for i in range(len(self.server)):
            server = self.server[i]
            conn = self.conn[i]

    def _fetch_pixels(self, index):
        """Re-populate pixel buffer"""
        conn = self.conn[index]
        server = self.server[index]
        data = conn.fetch()
        pxlbuff = None
        if data:
            pxlbuff = StringIO.StringIO(data)
        self.pxlbuff[index] = pxlbuff

    def _get_pixel(self, index):
        """Get a single pixel from the server[index]"""
        pxlbuff = self.pxlbuff[index]
        if not pxlbuff:
            return None # end of pixel streams from the server
        data = pxlbuff.read(16) # one pixel
        if not data:
            # buffer might be depleted, re-fill it, and repeat
            self._fetch_pixels(index)
            return self._get_pixel(index)
        p = byte_str_to_pixel(data)
        return p


    def __iter__(self):
        return self

    def next(self):
        if not self.heap:
            raise StopIteration()
        hent = heapq.heappop(self.heap)
        newpxl = self._get_pixel(hent.index)
        if newpxl:
            newhent = PxlHeapEntry(newpxl, hent.index)
            heapq.heappush(self.heap, newhent)
        return hent.pxl

# end of class ImgQuery


class MsgQuery(object):
    """Message query utility object.

    MsgQuery object is a utility to help query messages across bhttpds.

    Example:
        >>> mq = query.MsgQuery(host_ids = "1-10", ptn_ids="128-130",
        >>>                     ts0="1442943270", ts1="1442943270")
        >>> for x in mq:
        >>>     print x
    """
    def __init__(self, host_ids = None, ptn_ids = None, ts0 = None, ts1 = None):
        global _SERVERS;
        self.server = []
        self.session_id = []
        self.msg_buff = []
        self.heap = []
        self.conn = []
        self.params = {
            "type": "msg_simple",
            "n": 4096,
            "host_ids": host_ids,
            "ptn_ids": ptn_ids,
            "ts0": ts0,
            "ts1": ts1
        }

        self.N = len(_SERVERS)
        if not ts_fmt_check(ts0) or not ts_fmt_check(ts1):
            raise Exception("Unsupported timestamp format. Only '%Y-%m-%d %H:%M:%S' and 'seconds_since_epoch' are supported.");

        for s in _SERVERS:
            self.server.append(s)
            self.session_id.append(None)
            self.msg_buff.append(StringIO.StringIO())
            self.conn.append(BHTTPDConn(s))

        for i in range(self.N):
            msg = self._get_msg(i)
            if not msg:
                continue
            hent = MsgHeapEntry(msg, i)
            self.heap.append(hent)
        heapq.heapify(self.heap)

    def __del__(self):
        logger.debug("destroying MsgQuery")
        for i in range(len(self.server)):
            server = self.server[i]
            session_id = int(self.session_id[i])
            conn = self.conn[i]
            logger.debug("session_id: %d", session_id)
            if session_id:
                logger.debug("destroying session: %d", session_id)
                conn.req("/query/destroy_session", {"session_id": session_id})

    def _fetch_msgs(self, index):
        """Fetch a bunch of messages from server[index]"""
        conn = self.conn[index]
        server = self.server[index]
        session_id = self.session_id[index]
        msg_buff = self.msg_buff[index]
        if msg_buff == None:
            return None
        if msg_buff.tell() < msg_buff.len:
            # msg_buff not depleted
            raise Exception("msg_buff not depleted for server: "+server)

        params = self.params.copy()

        if session_id:
            params["session_id"] = session_id

        s = conn.req("/query", params)
        msg_buff = StringIO.StringIO(s)
        [tmp, session_id] = msg_buff.readline().split()
        self.session_id[index] = int(session_id)
        if msg_buff.len == msg_buff.tell():
            # End of messages
            self.msg_buff[index] = None
        else:
            self.msg_buff[index] = msg_buff
        return None

    def _get_msg(self, index):
        """Get a message from the server[index]"""
        msg_buff = self.msg_buff[index]
        if msg_buff == None:
            # end of messages from this server
            return None
        if msg_buff.tell() < msg_buff.len:
            return msg_buff.readline().rstrip()
        # reaching here means msg_buff depleted
        self._fetch_msgs(index)
        return self._get_msg(index) # retry


    def __iter__(self):
        return self

    def next(self):
        if not self.heap:
            raise StopIteration()
        hent = heapq.heappop(self.heap)
        newmsg = self._get_msg(hent.index)
        if newmsg:
            newhent = MsgHeapEntry(newmsg, hent.index)
            heapq.heappush(self.heap, newhent)
        return ' '.join([hent.ts, hent.host, hent.msg])

# end of MsgQuery

if __name__ == "__main__":
    import sys
    import argparse

    def handle_ptn(args):
        ptns = get_ptns()
        print_ptn_header()
        for ptn in ptns:
            print_ptn(ptn)
        print_ptn_footer()

    def handle_msg(args):
        if args.ptn_ids:
            args.ptn_ids = ','.join(args.ptn_ids)
        if args.host_ids:
            args.host_ids = ','.join(args.host_ids)
        mq = MsgQuery(ts0=args.ts_begin, ts1=args.ts_end,
                host_ids=args.host_ids, ptn_ids=args.ptn_ids)
        for msg in mq:
            print msg
        del mq

    def handle_img(args):
        if args.ptn_ids:
            args.ptn_ids = ','.join(args.ptn_ids)
        if args.host_ids:
            args.host_ids = ','.join(args.host_ids)
        q = ImgQuery(ts0=args.ts_begin, ts1=args.ts_end,
                host_ids=args.host_ids, ptn_ids=args.ptn_ids,
                img_store=args.img_store)
        for p in q:
            print p
        del q

    parser = argparse.ArgumentParser(
        description='bhttpd query command-line interface.'
    )
    parser.add_argument('-t', '--type', action='store',
        help='query type: msg, or ptn (default: msg)'
    )
    parser.add_argument('-P', '--ptn-ids', action='append',
        help='pattern-ids condition (e.g. "128-130,155")'
    )
    parser.add_argument('-H', '--host-ids', action='append',
        help='host-ids condition (e.g. "0-5,11-20")'
    )
    parser.add_argument('-B', '--ts-begin', action='store',
        help='Begin timestamp for msg query. (e.g. "2015-12-31 21:22:23")'
    )
    parser.add_argument('-E', '--ts-end', action='store',
        help='End timestamp for msg query. (e.g. "2015-12-31 21:22:23")'
    )
    parser.add_argument('-I', '--img-store', action='store',
        default='3600-1',
        help='Image store to query against (default: 3600-1)'
    )
    parser.add_argument('-S', '--bhttpd-servers', action='append',
        help='''List of bhttpd servers. If this argument is not set, the
        environment variable BHTTPD_SERVERS will be used. If the program cannot
        determine the list of bhttpd servers, it will exit with an error.
        (default: <empty>)'''
    )
    args = parser.parse_args()

    servers = args.bhttpd_servers

    if not servers:
        # try BHTTPD_SERVERS env var
        try:
            servers = [ os.environ['BHTTPD_SERVERS'] ]
        except:
            logger.error("""Cannot determine bhttpd servers.
                BHTTPD_SERVERS env var is not set, and -S is not specified.""")
            sys.exit(-1)

    add_servers(servers)

    handle_map = {
        'ptn': handle_ptn,
        'msg': handle_msg,
        'img': handle_img,
    }

    if not args.type:
        args.type = "msg"

    if args.type not in handle_map:
        logger.error("Cannot determine query type: %s" % args.type)
        sys.exit(-1)

    fn = handle_map[args.type]
    fn(args)
#EOF

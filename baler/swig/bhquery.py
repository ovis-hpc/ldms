#!/usr/bin/env python
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

class Msg(object):
    """Message wrapper, wrapping JSON msg returned from balerd server"""
    def __init__(self, msg):
        self.msg = msg

    def __str__(self):
        return ' '.join(self.msg.ts, self.msg.host, self.msg.msg)


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

class MsgQuery(object):
    """Message query utility object.

    MsgQuery object is a utility to help query messages across balerds.

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
    }

    if not args.type:
        args.type = "msg"

    if args.type not in handle_map:
        logger.error("Cannot determine query type: %s" % args.type)
        sys.exit(-1)

    fn = handle_map[args.type]
    fn(args)
#EOF

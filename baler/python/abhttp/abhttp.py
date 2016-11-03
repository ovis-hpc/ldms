#!/usr/bin/env python

# Copyright (c) 2016 Open Grid Computing, Inc. All rights reserved.
# Copyright (c) 2016 Sandia Corporation. All rights reserved.
#
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

"""Distributed Baler HTTP Daemon middle ware service.

This module contains classes and functions to communicate with multiple bhttpds
and manage their data as a whole.
"""

import heapq
import json
import logging
import os
import re
import yaml
import copy
from urlparse import urlparse

# Import names from the sub-modules
from datatype import *
from net import *
from coll import *

# Global service for abhttp daemon
svc = None

logger = logging.getLogger(__name__)
# logger.setLevel(logging.DEBUG)


class Config(object):
    """Configuration for Service.

    Synopsis:
        >>> cfg = Config.from_yaml_file(path)

    Configuration File (YAML) Format:
        sources: # declaring data sources
            <NAME0>: [<MAIN_URI0>, <MIRROR_URI0>, ...]
            <NAME1>: [<MAIN_URI1>, <MIRROR_URI1>, ...]
            ...
        store: <CLIENT_STORE_DIR>

    The source URIs can either be "http://" for bhttpd or "bstore://" for local
    machine baler store.

    The `store` directory is a directory for `bclient` service to save/load the
    aggregated pattern map and host map.

    The following configuration example has 4 data sources, 3 remote and 1
    local. The each of the remote data sources also has a mirror.

    Configuration Example:
        # config.yaml
        sources:
            bhttpd0: ["http://host0:18000", "http://host1:18000"]
            bhttpd1: ["http://host1:18001", "http://host0:18001"]
            bhttpd2: ["http://host2:18002", "http://host3:18002"]
            bstore0: ["bstore:///mnt/NVME0/bstore"]
        store: client.dir

    From the above configuration, bhttpd0 main instance is at host0:18000, and
    the mirror is at host1:18000. The Service will use the main instance if
    available. Otherwise, it will try the mirror. `bstore0` uses local baler
    store at `/mnt/NVME0/bstore`.
    """

    SOURCES_SECTION="sources"
    STORE_SECTION="store"

    def __init__(self, cfg=None):
        if not cfg:
            cfg = {}
        self._cfg = cfg
        self._parse_config()

    def __del__(self):
        pass

    def get_src_uris(self, name):
        return self._cfg[self.SOURCES_SECTION][name]

    def sources_iter(self):
        for (name, srcs) in self._cfg[Config.SOURCES_SECTION].iteritems():
            yield(name, srcs)

    def get_store(self):
        try:
            return self._cfg[self.STORE_SECTION]
        except KeyError:
            return None

    @staticmethod
    def from_yaml_stream(yaml_stream):
        return Config(cfg = yaml.load(yaml_stream))

    @staticmethod
    def from_yaml_file(yaml_file_path):
        f = open(yaml_file_path, "r")
        try:
            return Config.from_yaml_stream(f)
        finally:
            f.close()

    def _parse_config(self):
        b = self._cfg[Config.SOURCES_SECTION]
        for k in b:
            addrs = b[k]
            if type(addrs) != list:
                raise TypeError("bhttpd addresses should be 'list'.")
            for addr in addrs:
                if type(addr) != str:
                    raise TypeError("a bhttpd address should be 'str'.")
                m = re.match("^((http://[^:]+:\\d+)|(bstore:///.*))$", addr)
                if not m:
                    raise TypeError("Wrong address format.")


class Service(object):
    """Distributed Baler HTTP Service.

    ``Service`` is an object serving aggregated services of multiple bhttpds.

    For configuration file format, please see ``Config`` documentation.

    Args:
        cfg(Config): the Config object. If specified,
            ``cfg_stream`` and ``cfg_path`` will be ignored.
        cfg_stream(stream): configuration stream in YAML format. It can be
            a file object (from ``open()``) or ``str``. If specified,
            ``cfg_path`` parameter will be ignored.
        cfg_path(str): config file path.


    Attributes:
        uhost(UnifiedMapper): unified mapper for host
        uptn(UnifiedMapper): unified mapper for pattern
    """

    UPTN_PATH = "uptn.yaml"
    UHOST_PATH = "uhost.yaml"
    ConnClass = {
        "http": BHTTPDConn,
        "bstore": BStore2Conn
    }

    def __init__(self, cfg=None, cfg_stream=None, cfg_path=None):
        if not cfg:
            if not cfg_stream:
                if not cfg_path:
                    raise AttributeError(
                            "cfg, cfg_stream, and cfg_path are not specified")
                f = open(cfg_path, "r")
                try:
                    cfg_stream = f.read()
                finally:
                    f.close()
            cfg = Config.from_yaml_stream(cfg_stream)
        self._cfg = cfg
        self._conns = {}
        self.uhost = UnifiedMapper()
        self.uptn = UnifiedMapper()
        self.load_if_exists()
        self.init_connections()

    def init_connections(self):
        for (name, locs) in self._cfg.sources_iter():
            self._conns[name] = self.init_conn(name)

    def init_conn(self, name):
        """Initialize the bhttpd identified by ``name``."""
        srcs = self._cfg.get_src_uris(name)
        for src in srcs:
            try:
                url = urlparse(src)
            except Exception, e:
                logger.warn("url parse error: %s ... continue", e)
                continue

            logger.debug("name: %s, src: %s", name, src)

            try:
                ConnCls = self.ConnClass[url.scheme]
            except Exception:
                raise
                logger.warn("Unknown scheme: %s ... continue", url.scheme)
                continue

            try:
                conn = ConnCls(url.netloc + url.path, name=name)
            except Exception:
                logger.info("Service %s: location '%s' unavailable", name, src)
                continue
            else:
                logger.info("Connected to source '%s'", src)

            conn.get_host()
            h = conn.fetch_host()
            hmap = Mapper([(k, v) for (k,v) in h.items()])
            conn.get_ptn()
            p = conn.fetch_ptn()
            pmap = Mapper()
            for (k, ptn) in p.items():
                pmap.add(k, ptn.text)
                pmap.set_obj(_id=k, _obj=ptn)
            # Remove old mappers, if existed
            try:
                self.uhost.remove_mapper(name)
            except KeyError:
                pass
            try:
                self.uptn.remove_mapper(name)
            except KeyError:
                pass
            self.uhost.add_mapper(name, hmap)
            self.uptn.add_mapper(name, pmap)

            logger.info("Service %s: using location: %s", name, src)
            return conn

        # reaching here means all locations failed.
        # raise Exception("No connection to %s" % name)
        logger.warn("No connection to %s", name)
        return None

    def conn_iter(self):
        for (_name, _conn) in self._conns.items():
            yield (_name, _conn)

    def uhost_iter(self):
        for e in self.uhost:
            yield Host(e.id, e.str)

    def uhost_by_id(self, _id):
        _str = self.uhost.get_ustr(_id)
        if not _str:
            return None
        return Host(_id, _str)

    def uhost_by_str(self, _str):
        _id = self.uhost.get_uid(_str)
        if _id == None:
            return None
        return Host(_id, _str)

    def uhost_assign(self, uid, ustr):
        """Assign ``uid`` to the unified host ``ustr``."""
        self.uhost.assign(uid, ustr)

    def uhost_autoassign(self):
        self.uhost.auto_assign()

    def uptn_iter(self):
        for e in self.uptn:
            p = self.uptn_by_str(e.str)
            p.ptn_id = e.id
            yield p

    def uptn_by_str(self, _str):
        """Returns aggregated pattern by string.

        A pattern may appear in multiple sources. This function aggregate
        information about the queried pattern ``_str`` (such as count,
        first_seen, last_seen) from all sources and return as a Pattern object.
        """
        uid = self.uptn.get_uid(_str)
        pagg = None
        for m in self.uptn.mapper_iter():
            _id = m.get_id(_str)
            if _id == None:
                continue
            p = m.get_obj(_id)
            if pagg == None:
                pagg = Pattern(uid, p.count, p.first_seen, p.last_seen, p.text,
                               p.tokens)
            else:
                pagg += p
        return pagg

    def uptn_by_id(self, _id):
        _str = self.uptn.get_ustr(_id)
        return self.uptn_by_str(_str)

    def uptn_autoassign(self):
        self.uptn.auto_assign()

    def uptn_assign(self, uid, ustr):
        """Assign ``uid:ustr`` mapping in the unified pattern mapper."""
        self.uptn.assign(uid, ustr)

    def uptn_update(self):
        """Update unified patterns.

        Update individual mapping from each connection, and update the pattern
        unified mapper to recognize the new patterns.

        The new patterns do not have IDs yet.
        """
        def _update_ptn_gen(conn):
            conn.get_ptn()
            p = conn.fetch_ptn()
            for (k, ptn) in p.items():
                yield (k, ptn.text, ptn)
        for (name, conn) in self._conns.items():
            self.uptn.update_mapper(name, _update_ptn_gen(conn))

    def uhost_update(self):
        """Update hosts.

        Update host mapping for each connection, and update the unified mapper
        (host) to recognize the new hosts.

        The new hosts in the unified mapper do not yet have IDs assigned.
        """
        def _update_host_gen(conn):
            conn.get_host()
            p = conn.fetch_host()
            for (k, host) in p.items():
                yield (k, host, None)
        for (name, conn) in self._conns.items():
            self.uhost.update_mapper(name, _update_host_gen(conn))

    def save(self):
        """Save the unified mappers (uptn, uhost)."""
        store = self._cfg.get_store()
        if not os.path.isdir(store):
            # try to create the store.
            os.makedirs(store, mode=0755)
        self.uptn.save("%s/%s" % (store, self.UPTN_PATH))
        self.uhost.save("%s/%s" % (store, self.UHOST_PATH))

    def load(self):
        """Load the unified mappers (uptn, uhost)."""
        store = self._cfg.get_store()
        self.uptn.load("%s/%s" % (store, self.UPTN_PATH))
        self.uhost.load("%s/%s" % (store, self.UHOST_PATH))

    def load_if_exists(self):
        store = self._cfg.get_store()
        self.uptn.load_if_exists("%s/%s" % (store, self.UPTN_PATH))
        self.uhost.load_if_exists("%s/%s" % (store, self.UHOST_PATH))

    def __del__(self):
        pass


class UMsgQueryIter(object):
    """Unified Message Query Iterator.

    Iterator for unified result of bhttpd message queries.

    NOTE: We assume that the ``host`` in the iterators are mutually exclusive.
    """

    def __init__(self, service, host_ids=None, ptn_ids=None,
                 ts0=None, ts1=None):
        """Constructor.

        Args:
            service(Service): Service object.
            host_ids(str): host_ids to query.
            query(dict): dictionary containing query parameters.
        """
        self._service = service
        self._heap = []
        self._eof_entries = dict()
        for (_name, _conn) in service.conn_iter():
            _host_ids = self._translate_ids( _name, service.uhost, host_ids)
            _ptn_ids = self._translate_ids( _name, service.uptn, ptn_ids)
            if ptn_ids and not _ptn_ids:
                # skip iterators that cannot participate
                continue
            if host_ids and not _host_ids:
                # skip iterators that cannot participate
                continue

            itr = BHTTPDConnMsgQueryIter(
                            conn=_conn,
                            host_ids=_host_ids,
                            ptn_ids=_ptn_ids,
                            ts0=ts0,
                            ts1=ts1
                        )
            self._eof_entries[_name] = itr
        if not self._eof_entries:
            raise Exception("this is so sad")
        # Last direction
        self._dir = None
        self._curr_msg = None

    def __iter__(self):
        msg = self.next()
        while msg:
            yield msg
            msg = self.next()

    def _translate_ids(self, target, umap, ids):
        if ids == None:
            return None
        oset = IDSet() # original ID set
        tset = IDSet() # target ID set
        oset.add_smart(ids)
        for _id in oset:
            tid = umap.translate_id(umap.UNIFIED, target, _id)
            if tid != None:
                tset.add(tid)
        if not tset:
            return None
        return tset.to_csv()

    def _update_curr_msg(self):
        if (self._heap):
            itr = self._heap[0]
            # make a copy so that we can modify ptn_id w/o affecting the
            # original message object.
            msg = copy.copy(itr.get_curr_msg())
            name = itr.get_conn_name()
            svc = self._service
            msg.ptn_id = svc.uptn.translate_id(name, svc.uptn.UNIFIED, msg.ptn_id)
            msg.pos = None
            self._curr_msg = msg
        else:
            self._curr_msg = None

    def _reverse_direction(self):
        tmp = []
        while self._heap:
            itr = self._heap.pop()
            tmp.append(itr)
        while self._eof_entries:
            (name, itr) = self._eof_entries.popitem()
            tmp.append(itr)
        for itr in tmp:
            if self._dir == BWD:
                m = itr.next()
            elif self._dir == FWD:
                m = itr.prev()
            else:
                assert(False)
            if m:
                self._heap.append(itr)
            else:
                name = itr.get_conn_name()
                self._eof_entries[name] = itr
        heapq.heapify(self._heap)
        self._update_curr_msg()
        pass

    def next(self):
        """Get next unified message."""
        if self._dir != FWD:
            self._dir = BWD
            self._reverse_direction()
            self._dir = FWD
            return self._curr_msg
        if not self._heap:
            return None
        itr = heapq.heappop(self._heap)
        if itr.next():
            heapq.heappush(self._heap, itr)
        else:
            name = itr.get_conn_name()
            self._eof_entries[name] = itr
        self._update_curr_msg()
        return self._curr_msg

    def prev(self):
        """Get previous unified message."""
        if self._dir != BWD:
            self._dir = FWD
            self._reverse_direction()
            self._dir = BWD
            return self._curr_msg
        if not self._heap:
            return None
        itr = heapq.heappop(self._heap)
        if itr.prev():
            heapq.heappush(self._heap, itr)
        else:
            name = itr.get_conn_name()
            self._eof_entries[name] = itr
        self._update_curr_msg()
        return self._curr_msg

    def get_curr_msg(self):
        return self._curr_msg

    def get_pos(self):
        """Get ``str`` representation of iterator position."""
        buff = StringIO.StringIO()
        buff.write(self._dir)
        buff.write(" ")
        first = True
        for itr in self._heap:
            ipos = itr.get_pos()
            iname = itr.get_conn_name()
            if not first:
                buff.write(" ")
            first = False
            buff.write(iname)
            buff.write("|")
            buff.write(ipos)
        return buff.getvalue()
        # ignore the EOF iterators.

    def set_pos(self, pos=str()):
        """Recover the iterator to the given position."""
        # reset all iterators in the heap, make them all EOF.
        # then, recover position.
        while self._heap:
            itr = self._heap.pop()
            itr.reset()
            name = itr.get_conn_name()
            self._eof_entries[name] = itr

        posstream = pos.split(" ")
        positr = iter(posstream)

        _dir = next(positr)

        self._dir = _dir

        for x in positr:
            (name, ipos) = x.split("|")
            itr = self._eof_entries.pop(name)
            m = itr.set_pos(ipos, direction=_dir)
            assert(m)
            self._heap.append(itr)

        if self._heap:
            heapq.heapify(self._heap)
            self._curr_msg = self._heap[0].get_curr_msg()

##---- UMsgQueryIter ----##


class UImgQueryIter(object):
    """Query image data, and obtain the results in iterator fashion.

    NOTE: This only supports forward iterator as there is no use-case for the
    backward iterator (yet).
    """

    def __init__(self, service, img_store, host_ids=None, ptn_ids=None,
                 ts0=None, ts1=None):
        self.img_store = img_store
        self.host_ids = host_ids
        self.ptn_ids = IDSet(ptn_ids) if ptn_ids else [
                            e.id for e in service.uptn if e.id != None
                        ]
        self.ts0 = ts0
        self.ts1 = ts1
        self.service = service

    def _translate_ids(self, target, umap, ids):
        if ids == None:
            return None
        oset = IDSet() # original ID set
        tset = IDSet() # target ID set
        oset.add_smart(ids)
        for _id in oset:
            tid = umap.translate_id(umap.UNIFIED, target, _id)
            if tid != None:
                tset.add(tid)
        if not tset:
            return None
        return tset.to_csv()

    def __iter__(self):
        service = self.service
        for uptn_id in self.ptn_ids:
            pxl_buff = {}
            for (_name, _conn) in service.conn_iter():
                _host_ids = self._translate_ids(_name, service.uhost,
                                                self.host_ids)
                if self.host_ids and not _host_ids:
                    # skip iterators that cannot participate
                    continue
                ptn_id = service.uptn.translate_id(UnifiedMapper.UNIFIED,
                                                   _name,
                                                   uptn_id
                                            )
                if ptn_id == None:
                    continue
                _conn.get_img(img_store=self.img_store,
                                host_ids=_host_ids,
                                ptn_ids=ptn_id,
                                ts0=self.ts0,
                                ts1=self.ts1
                            )
                for pxl in _conn.fetch_img():
                    ucomp_id = service.uhost.translate_id(_name,
                                                UnifiedMapper.UNIFIED,
                                                pxl.key.comp_id
                                            )
                    key = PixelKey(ptn_id=uptn_id,
                                    sec=pxl.key.sec,
                                    comp_id=ucomp_id
                                )
                    try:
                        apxl = pxl_buff[key]
                    except KeyError:
                        pxl_buff[key] = apxl = Pixel(pxl.key.sec,
                                                     ucomp_id, uptn_id, 0)
                    apxl.count += pxl.count
            pxls = pxl_buff.values()
            pxls.sort()
            for pxl in pxls:
                yield pxl

##---- UImgQueryIter ----##

# EOF

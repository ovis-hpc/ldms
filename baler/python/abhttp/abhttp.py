#!/usr/bin/env python
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
        bhttpd: # declaring bhttpd section
            <NAME0>: [<MAIN_ADDR0>, <MIRROR_ADDR>, ...]
            <NAME1>: [<MAIN_ADDR1>, <MIRROR_ADDR>, ...]
            ...

    The following configuration example has 2 pair of hosts. Each host in the
    pair hosts two bhttpd: the main instance and a mirror of its pair.

    Configuration Example:
        # config.yaml
        bhttpd:
            bhttpd0: ["host0:18000", "host1:18000"]
            bhttpd1: ["host1:18001", "host0:18001"]
            bhttpd2: ["host2:18002", "host3:18002"]
            bhttpd3: ["host3:18003", "host2:18003"]

    From the above configuration, bhttpd0 main instance is at host0:18000, and
    the mirror is at host1:18000. The Service will use the main instance if
    available. Otherwise, it will try the mirror.
    """

    BHTTPD_SECTION="bhttpd"
    STORE_SECTION="store"

    def __init__(self, cfg=None):
        if not cfg:
            cfg = {}
        self._cfg = cfg
        self._parse_config()

    def __del__(self):
        pass

    def bhttpd_get_locations(self, name):
        return self._cfg[self.BHTTPD_SECTION][name]

    def bhttpd_iter(self):
        class _bhttpd_iter(object):
            def __init__(_self, _cfg):
                _self._bhttpd = _cfg._cfg[Config.BHTTPD_SECTION]
                _self._itr = iter(_self._bhttpd)
            def __iter__(_self):
                return _self
            def next(_self):
                k = next(_self._itr)
                v = _self._bhttpd[k]
                return (k, v)
        return _bhttpd_iter(self)

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
        b = self._cfg[Config.BHTTPD_SECTION]
        for k in b:
            addrs = b[k]
            if type(addrs) != list:
                raise TypeError("bhttpd addresses should be 'list'.")
            for addr in addrs:
                if type(addr) != str:
                    raise TypeError("a bhttpd address should be 'str'.")
                m = re.match("^[^:]+:\\d+", addr)
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
        self.init_bhttpd_connections()

    def init_bhttpd_connections(self):
        for (name, locs) in self._cfg.bhttpd_iter():
            self._conns[name] = self.init_bhttpd_conn(name)

    def init_bhttpd_conn(self, name):
        """Initialize the bhttpd identified by ``name``."""
        locs = self._cfg.bhttpd_get_locations(name)
        for loc in locs:
            logger.debug("name: %s, loc: %s", name, loc)
            try:
                conn = BHTTPDConn(loc, name=name)
            except Exception:
                logger.info("Service %s: location '%s' unavailable", name, loc)
                continue
            else:
                logger.info("Connected to location '%s'", loc)

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

            logger.info("Service %s: using location: %s", name, loc)
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
        pagg = Pattern(uid, 0, None, None, _str)
        for m in self.uptn.mapper_iter():
            _id = m.get_id(_str)
            if _id == None:
                continue
            p = m.get_obj(_id)
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

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

import os
import cmd
import logging
import abhttp
import shlex
import re
import datetime
import sys
import curses
import collections
import ptn_order
import subprocess
import time
import errno
from StringIO import StringIO

logger = logging.getLogger(__name__)

service = None

class Debug(object):
    pass

# Global debug object.
DBG = Debug()

def bclient_init(cfg_path):
    global service
    if service != None:
        logger.warn("bclient has already been initialized.")
        return
    service = abhttp.Service(cfg_path=cfg_path)


class CmdException(Exception):
    pass


class CmdArgumentParser(object):
    def __init__(self, *args):
        """Initialize a command argument parser.

        Args:
            arg_spec: [(name, type, value_hint, bool)] - a list of 4-tuple
                        describing argument's name, type, value hint and if the
                        argument is required respectively.
        """
        self.kwargs_type = {}
        self.kwargs_hint = {}
        self.required_kwargs = []
        self.available_kwargs = set()
        for (_name, _type, _hint, _required) in args:
            self.kwargs_type[_name] = _type
            self.kwargs_hint[_name] = _hint
            self.available_kwargs.add(_name)
            if _required:
                self.required_kwargs.append(_name)

    def parse(self, arg=str()):
        """Parse the command arguments.

        Returns:
            tuple(map(), list()) for keyword arguments and positional
                        arguments respectively. The keywoard argument map will
                        also contain optional arguments with value `None` if the
                        option is not given. This is to conveniently avoid
                        `KeyError` exception.
        """
        _kwargs = {}
        _args = []
        _redirect = ""
        _pipe = []
        _redir_count = 0
        shlexargs = shlex.split(arg)
        itr = iter(shlexargs)
        for a in itr:
            if a.startswith("|"):
                # pipe
                _pipe.append(a)
                for b in itr: # exhaust the reset of the args
                    _pipe.append(b)
            if a.startswith(">"):
                # redirection
                if _redir_count:
                    raise CmdException("Too many redirections")
                _redirect = a
                if a == ">" or a == ">>":
                    # consume next arg if redirect
                    _redirect += next(itr)
                _redir_count += 1
            x = a.split('=', 1)
            if len(x) == 1:
                # positional arg
                _args.append(a)
            else:
                # check if keyword is valid
                if x[0] not in self.available_kwargs:
                    raise CmdException("Invalid keyword: %s" % x[0])
                _kwargs[x[0]] = self.kwargs_type[x[0]](x[1])
        for kw in self.required_kwargs:
            if kw not in _kwargs:
                raise CmdException("Required keyword not specified: %s" % kw)
        for kw in self.available_kwargs:
            if kw not in _kwargs:
                _kwargs[kw] = None
        return (_kwargs, _args, _redirect, _pipe)

## ---- CmdArgumentParser ---- ##


class FakeIterator():
    def __init__(self, begin=0, end=1000):
        self._begin = begin
        self._end = end
        self.current = self._begin - 1

    def next(self):
        if self.current >= self._end:
            return None
        if self.current < self._begin:
            self.current = self._begin - 1
        self.current += 1
        return self.current

    def prev(self):
        if self.current <= self._begin:
            return None
        if self.current > self._end:
            self.current = self._end + 1
        self.current -= 1
        return self.current

    def get_pos(self):
        return self.current

    def set_pos(self, pos):
        self.current = pos


class PageDisplay(object):
    def __init__(self, itr):
        self.itr = itr
        self.active = True
        self.buff = collections.deque()
        self.dir = None

    def _start(self):
        self.win = curses.initscr()
        curses.noecho()
        curses.cbreak()
        self.active = True

    def _stop(self):
        self.active = False

    def _end(self):
        curses.nocbreak()
        curses.echo()
        curses.endwin()

    def loop(self):
        cmd_table = {
            'u': self.page_up,
            'd': self.page_down,
            'q': self._stop,
            'j': self.line_next,
            'k': self.line_prev,
        }
        self._start()
        try:
            self.dir = abhttp.FWD
            self.page_down()
            while self.active:
                c = self.win.getkey()
                try:
                    fn = cmd_table[c]
                except KeyError:
                    continue
                fn()
        finally:
            self._end()

    def display_buff(self):
        maxy, maxx = self.getmaxyx()
        DBG.disp_buff = self.buff
        self.win.clear() # not refeshed yet
        y = 0
        # self.buff length is maxy-1
        for (pos, item) in self.buff:
            s = str(item)
            DBG.item = item
            DBG.item_s = s
            self.win.addstr(y, 0, s[:maxx])
            y += 1
        self.win.addstr(y, 0, ':(j-next_line, k-prev_line, d-pg_down, u-pg_up, q-quit)')
        self.win.refresh()

    def getmaxyx(self):
        maxy, maxx = self.win.getmaxyx()
        return (maxy-1, maxx)

    def line_next(self):
        maxy, maxx = self.getmaxyx()
        if self.dir != abhttp.FWD:
            # need position recovery
            (pos, item) = self.buff[len(self.buff)-1]
            self.itr.set_pos(pos)
            self.dir = abhttp.FWD
        item = self.itr.next()
        if item == None:
            return
        pos = self.itr.get_pos()
        self.buff.append((pos, item))
        while len(self.buff) > maxy:
            self.buff.popleft()
        self.display_buff()

    def line_prev(self):
        maxy, maxx = self.getmaxyx()
        if self.dir != abhttp.BWD:
            # need position recovery
            (pos, item) = self.buff[0]
            self.itr.set_pos(pos)
            self.dir = abhttp.BWD
        item = self.itr.prev()
        if item == None:
            return
        pos = self.itr.get_pos()
        self.buff.appendleft((pos, item))
        while len(self.buff) > maxy:
            self.buff.pop()
        self.display_buff()

    def page_down(self):
        maxy, maxx = self.getmaxyx()
        c = 0
        while c < maxy:
            self.line_next()
            c += 1

    def page_up(self):
        maxy, maxx = self.getmaxyx()
        c = 0
        while c < maxy:
            self.line_prev()
            c += 1


term_color = {
    "PURPLE": '\033[95m',
    "CYAN": '\033[96m',
    "DARKCYAN": '\033[36m',
    "BLUE": '\033[94m',
    "GREEN": '\033[92m',
    "YELLOW": '\033[93m',
    "RED": '\033[91m',
    "BOLD": '\033[1m',
    "UNDERLINE": '\033[4m',
    "END": '\033[0m'
}


class ServiceCmd(cmd.Cmd):
    def __init__(self):
        cmd.Cmd.__init__(self)
        self.prompt = "bclient> "
        self.parsers = {}

    def get_parser(self, cmd):
        if cmd in self.parsers:
            return self.parsers[cmd]
        try:
            pf = getattr(self, "parser_" + cmd)
        except AttributeError:
            return None
        else:
            try:
                p = pf()
                self.parsers[cmd] = p
                return p
            except Exception, e:
                logger.debug(e)

    def parser_init(self):
        return CmdArgumentParser(("config", str, "PATH", 1))

    def do_init(self, arg):
        """init config=<PATH>

        Initialize bclient with a given config file."""
        (kwargs, args) = (self.kwargs, self.args)
        bclient_init(kwargs['config'])

    def parser_ptn_query(self):
        return CmdArgumentParser(
            ("text", str, "REGEX", 0),
            ("ids", str, "RANGES", 0),
            ("format", str, "FMT", 0),
            ("order", str, "FIELD_ORDER", 0),
        )

    def do_ptn_query(self, arg):
        """ptn_query [text=REGEX] [ids=RANGES] [format=FMT] [order=KEY_ORDER] [>OUTPUT_FILE]

        Query the unified map for patterns that match all of the given
        conditions.

        Conditions:
            text=REGEX is the regular expression to match pattern text (e.g.
            ".*error.*").

            ids=RANGES is the comma-separated ranges to match IDs (e.g.
            "1,3-5,11").

        Optional options:
            format=FMT chooses what to print. By default, the FMT is
            "%(ptn_id)s %(count)s %(first_seen)s %(last_seen)s %(text)s".

            order=KEY_ORDER orders the results according to the KEY and ORDER.
            The `ORDER` is either "asc" or "desc". The `KEY` is one of the
            following: ptn_id, count, first_seen, last_seen, eng. Examples of
            KEY_ORDER are "ptn_id_asc", "last_seen_desc", and "eng_desc".

            >OUTPUT_FILE redirects the query results into a file (overwrite).
            Simply add `>OUTPUT_PATH` at the end of the command (OUTPUT_PATH is
            the path to the file), and the query results will be redirected
            there.
        """
        global service
        (kwargs, args) = (self.kwargs, self.args)
        text = kwargs["text"]
        ids = kwargs["ids"]
        fmt = kwargs["format"]
        order = kwargs["order"]
        if text:
            text = re.compile(text)
        if ids:
            ids = abhttp.IDSet(ids)
        result = []
        for ptn in service.uptn_iter():
            if ids and ptn.ptn_id not in ids:
                continue
            if text and not text.match(ptn.text):
                continue
            DBG.ptn = ptn
            result.append(ptn)
        if order:
            _cmp = ptn_order.get_ptn_cmp(order)
            result.sort(_cmp)
        for ptn in result:
            print >>self.cmdout, ptn.format(fmt)

    def parser_host_query(self):
        return CmdArgumentParser(
            ("text", str, "REGEX", 0),
            ("ids", str, "RANGES", 0),
        )

    def do_host_query(self, arg):
        """shost_query [text=REGEX] [ids=RANGES] [>OUTPUT_PATH]

        Query the unified map for hosts that match all of the given conditions.

        Conditions:
            text - regular expression to match host names (e.g. "node00.*").
            ids - comma-separated ranges to match IDs (e.g. "1,3-5,11").

        Optional output redirection:
            host_query command can redirect the query results into a file
            (overwrite). Simply add `>OUTPUT_PATH` at the end of the command
            (OUTPUT_PATH is the path to the file), and the query results will be
            redirected there.
        """
        global service
        (kwargs, args) = (self.kwargs, self.args)
        text = kwargs["text"]
        ids = kwargs["ids"]
        if text:
            text = re.compile(text)
        if ids:
            ids = abhttp.IDSet(ids)
        result = []
        for h in service.uhost_iter():
            if ids and h.host_id not in ids:
                continue
            if text and not text.match(h.text):
                continue
            result.append(h)
        result.sort()
        for h in result:
            print >>self.cmdout, h.host_id, h.text

    def parser_ptn_assign(self):
        return CmdArgumentParser(
                ("text", str, "FULL_PATTERN", 0),
                ("id", int, "NUM", 0),
            )

    def do_ptn_assign(self, arg):
        """ptn_assign [text=STR] [id=NUM]

        Assign the `id` to a pattern specified by `text` keywoard. If `id` is
        not specified, then bclient automatically assign an available ID to the
        pattern.

        If `text` and `id` are not given, pattern IDs will be automatically
        assigned to all of the patterns that do not yet have IDs.

        Other combination of arguments are considered invalid.
        """
        global service
        (kwargs, args) = (self.kwargs, self.args)
        text = kwargs['text']
        iD = kwargs['id']
        if not text:
            # automatic assignment
            if iD != None:
                # invalid
                logger.warn("Invalid arguments")
                return
            service.uptn_autoassign()
            return
        # single assignment
        if iD == None:
            iD = service.uptn.get_max_id() + 1
        service.uptn_assign(iD, text)

    def parser_host_assign(self):
        return CmdArgumentParser(
                ("text", str, "HOST_NAME", 0),
                ("id", int, "NUM", 0),
            )

    def do_host_assign(self, arg):
        """host_assign [text=STR] [id=NUM]

        Assign the `id` to a host specified by `text` keywoard. If `id` is
        not specified, then bclient automatically assign an available ID to the
        host.

        If `text` and `id` are not given, host IDs will be automatically
        assigned to all of the hosts that do not yet have IDs.

        Other combination of arguments are considered invalid.
        """
        global service
        (kwargs, args) = (self.kwargs, self.args)
        text = kwargs['text']
        iD = kwargs['id']
        if not text:
            # automatic assignment
            if iD != None:
                # invalid
                logger.warn("Invalid arguments")
                return
            service.uhost_autoassign()
            return
        # single assignment
        if iD == None:
            iD = service.uptn.get_max_id() + 1
        service.uhost_assign(iD, text)

    def parser_save(self):
        return CmdArgumentParser()

    def do_save(self, arg):
        """Save changes of the unified pattern/host mapper into the store."""
        global service
        service.save()

    def parser_remote_refresh(self):
        return CmdArgumentParser()

    def do_test_paging(self, arg):
        """Test Paging"""
        itr = FakeIterator()
        p = PageDisplay(itr)
        p.loop()
        pass

    def do_remote_refresh(self, arg):
        """Refreshing the unified data, re-aggregating remote information."""
        global service
        (kwargs, args) = (self.kwargs, self.args)
        service.uptn_update()
        service.uhost_update()

    def parser_msg_query(self):
        return CmdArgumentParser(
            ("ptn_ids", str, "RANGES", 0),
            ("host_ids", str, "RANGES", 0),
            ("begin", abhttp.Timestamp.fromStr, "TIMESTAMP", 0),
            ("end", abhttp.Timestamp.fromStr, "TIMESTAMP", 0),
        )

    def do_msg_query(self, arg):
        """msg_query [ptn_ids=RANGES] [host_ids=RANGES] [begin=TIMESTAMP]
                     [end=TIMESTAMP]

        Query messages matching the given conditions.

        Conditions:
            ptn_ids=RANGES is the ranges of pattern IDs from the unified map
            (e.g.  "1,3-5,11").

            host_ids=RANGES is the ranges of host IDs from the unified map (e.g.
            "1,3-5,11").

            begin=TIMESTAMP constrains the result messages to have timestamp
            greater than or equals to the given TIMESTAMP.

            end=TIMESTAMP constrains the result messages to have timestamp less
            than or equals to the given TIMESTAMP.

            The TIMESTAMP format is "yyyy-mm-ddTHH:MM:SS[(+|-)HH:MM]".  It is
            the same timestamp format appeared in the results of `ptn_query` and
            `msg_query`, excluding the microsecond part. The microsecond can be
            put in, but will be ignored as currently our time index does not
            include the microsecond part. If the timezone (the trailing "-HH:MM"
            or "+HH:MM") is omitted, the system timezone is used.
        """
        global service
        (kwargs, args) = (self.kwargs, self.args)
        ts0 = kwargs["begin"]
        if ts0:
            ts0 = int(ts0.sec)
        ts1 = kwargs["end"]
        if ts1:
            ts1 = int(ts1.sec)
        itr = abhttp.UMsgQueryIter(service, host_ids=kwargs["host_ids"],
                                            ptn_ids=kwargs["ptn_ids"],
                                            ts0=ts0,
                                            ts1=ts1
                                        )
        if self.cmdout.isatty():
            # use paging
            pager = PageDisplay(itr)
            pager.loop()
        else:
            for msg in itr:
                print >>self.cmdout, str(msg)

    def parser_img_query(self):
        return CmdArgumentParser(
            ("store", str, "3600-1", 1),
            ("ptn_ids", str, "RANGES", 0),
            ("host_ids", str, "RANGES", 0),
            ("begin", abhttp.Timestamp.fromStr, "TIMESTAMP", 0),
            ("end", abhttp.Timestamp.fromStr, "TIMESTAMP", 0),
        )

    def do_img_query(self, arg):
        """img_query [store=SPP-NPP] [ptn_ids=RANGES] [host_ids=RANGES]
                     [begin=TIMESTAMP] [end=TIMESTAMP] [>OUTPUT_FILE]

        Query image pixels per given conditions.

        The OUTPUT format is a list of pixels, one pixel per line, described
        by: PTN_ID, UNIX_TIMESTAMP, HOST_ID, COUNT.

        The results can be redirected into an OUTPUT_FILE by giving
        `>OUTPUT_FILE` at the end of the command.

        Conditions:
            store=SPP-NPP tells `bquery` to get the pixels from the specified
            image sub-store. (e.g. "3600-1" for pixels of 1-hour x 1-node).
            baler daemon can have multiple image stores depending on `balerd`
            configuration. Please refer to `balerd(1)` configuration for more
            information.

            ptn_ids=RANGES is the comma-separated ranges of pattern IDs (e.g.
            "1,3-5,11").

            host_ids=RANGES is the comma-separated ranges of host IDs (e.g.
            "1,3-5,11").

            begin=TIMESTAMP constrains the result pixels to have timestamp
            greater than or equals to the given TIMESTAMP.

            end=TIMESTAMP constrains the result pixels to have timestamp less
            than or equals to the given TIMESTAMP.

            The TIMESTAMP format is "yyyy-mm-ddTHH:MM:SS[(+|-)HH:MM]".  It is
            the same timestamp format appeared in the results of `ptn_query` and
            `msg_query`, excluding the microsecond part. The microsecond can be
            put in, but will be ignored as currently our time index does not
            include the microsecond part. If the timezone (the trailing "-HH:MM"
            or "+HH:MM") is omitted, the system timezone is used.
        """
        global service
        (kwargs, args) = (self.kwargs, self.args)
        ts0 = kwargs["begin"]
        if ts0:
            ts0 = ts0.sec
        ts1 = kwargs["end"]
        if ts1:
            ts1 = ts1.sec
        for pxl in abhttp.UImgQueryIter(service, img_store=kwargs["store"],
                                        host_ids=kwargs["host_ids"],
                                        ptn_ids=kwargs["ptn_ids"],
                                        ts0=ts0,
                                        ts1=ts1
                                    ):
            print >>self.cmdout, str(pxl)

    def do_EOF(self, arg):
        """Stop the command intepreter."""
        return True

    def do_quit(self, arg):
        """Stop the command intepreter."""
        return True

    def do_exit(self, arg):
        """Stop the command intepreter."""
        return True

    def emptyline(self):
        """Do nothing on empty line."""
        pass

    def _pipe(self, pipe_array):
        try:
            pipe_array[0] = pipe_array[0].strip("|")
            command = " ".join(pipe_array)
            self.proc = subprocess.Popen([command],
                                        stdin=subprocess.PIPE,
                                        stdout=None,
                                        shell=True,
                                        close_fds=True)
            self.cmdout = self.proc.stdin
        except Exception, e:
            raise CmdException(str(e))

    def _redirect(self, redir_str):
        # redir_str is the argument starts with >
        try:
            out = redir_str
            out_name = out.strip(">")
            if out.startswith(">>"):
                out = open(out_name, "a")
            else:
                out = open(out_name, "w")
            self.cmdout = out
        except Exception, e:
            raise CmdException(str(e))

    def precmd(self, line):
        """Apply our parser and store results in self.kwargs, self.args."""
        (self.kwargs, self.args) = ({}, [])
        self.t0 = time.time()
        self.cmdout = sys.stdout
        self.proc = None
        if not line:
            return line
        tmp = line.split(None, 1)
        cmd = tmp[0]
        arg = "" if len(tmp) == 1 else tmp[1]
        p = self.get_parser(cmd)
        if p:
            (self.kwargs, self.args, redirect, pipe) = p.parse(arg)

            if pipe and redirect:
                raise CmdException("Cannot use pipe after redirect.")

            self.cmdout = sys.stdout

            if redirect:
                self._redirect(redirect)

            if pipe:
                self._pipe(pipe)

        return line # return same line for Cmd processing.

    def postcmd(self, stop, line):
        if self.cmdout != self.stdout:
            self.cmdout.close()
            self.cmdout = self.stdout # reset cmdout
        if self.proc:
            self.proc.stdin.close()
            self.proc.wait()
            self.proc = None
        self.t1 = time.time()
        logger.debug("cmd time: %f", (self.t1 - self.t0))
        return stop

    def completedefault(self, text, line, begidx, endidx):
        cmd = line.split()[0]
        p = self.get_parser(cmd)
        if not p:
            return cmd.Cmd.completedefault(self, text, line, begidx, endidx)
        c = [x+"="+str(p.kwargs_hint[x]) \
                        for x in p.available_kwargs if x.startswith(text)]
        if len(c) == 1:
            x = c[0].split('=', 1)[0]
            c = [x+'=']
        return c

if __name__ == "__main__":
    # Setup basic logging if this is a main service.
    LOGFMT = "%(asctime)s %(name)s %(levelname)s: %(message)s"
    logging.basicConfig(format=LOGFMT)

    import argparse
    ap = argparse.ArgumentParser(description="Baler Service Client.")
    ap.add_argument("-c", "--config", action="store",
                    default=None, type=str,
                    help="Config file path."
            )
    ap.add_argument("-v", "--verbose", action="store",
                    default="INFO", type=str,
                    help="Verbosity level (DEBUG, INFO, WARN, ERROR, CRITICAL)"
            )
    args = ap.parse_args()
    logger.setLevel(logging.getLevelName(args.verbose.upper()))
    abhttp.logger.setLevel(logging.getLevelName(args.verbose.upper()))

    if args.config:
        bclient_init(cfg_path=args.config)

    svc_cmd = ServiceCmd()
    while True:
        try:
            svc_cmd.cmdloop()
        except CmdException, e:
            # in case of exception, print it and continue the loop
            print >>sys.stderr, "Error:", e
        except IOError, e:
            # ignore broken pipe
            if e.errno != errno.EPIPE:
                raise
        else:
            # cmdloop exit cleanly, exit the program.
            break
    logger.debug("END OF PROGRAM.")

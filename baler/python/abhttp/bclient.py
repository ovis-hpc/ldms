#!/usr/bin/env python
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
from StringIO import StringIO

logger = logging.getLogger(__name__)

service = None

def bclient_init(cfg_path):
    global service
    if service != None:
        logger.warn("bclient has already been initialized.")
        return
    service = abhttp.Service(cfg_path=cfg_path)
    try:
        service.load()
    except Exception:
        # it is OK to fail.
        pass

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
        for a in shlex.split(arg):
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
        return (_kwargs, _args)

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
            'n': self.next_page,
            'p': self.prev_page,
            'q': self._stop,
            'j': self.next_line,
            'k': self.prev_line,
        }
        self._start()
        try:
            self.dir = abhttp.FWD
            self.next_page()
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
        self.win.clear() # not refeshed yet
        y = 0
        for (pos, item) in self.buff:
            s = str(item)
            self.win.addstr(y, 0, s)
            y += 1
        self.win.refresh()

    def next_line(self):
        maxy, maxx = self.win.getmaxyx()
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

    def prev_line(self):
        maxy, maxx = self.win.getmaxyx()
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

    def next_page(self):
        maxy, maxx = self.win.getmaxyx()
        c = 0
        while c < maxy:
            self.next_line()
            c += 1

    def prev_page(self):
        maxy, maxx = self.win.getmaxyx()
        c = 0
        while c < maxy:
            self.prev_line()
            c += 1


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
        )

    def do_ptn_query(self, arg):
        """ptn_query [text=regex] [ids=ranges]

        Query the unified pattern table.
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
        for ptn in service.uptn_iter():
            if ids and ptn.ptn_id not in ids:
                continue
            if text and not text.match(ptn.text):
                continue
            print >>self.cmdout, str(ptn)

    def parser_host_query(self):
        return CmdArgumentParser(
            ("text", str, "REGEX", 0),
            ("ids", str, "RANGES", 0),
        )

    def do_host_query(self, arg):
        """host_query [text=regex] [ids=ranges]

        Query hosts that match all of the given conditions.

        Conditions:
            text - regular expression to match host names (e.g. "node00.*").
            ids - comma-separated ranges to match IDs (e.g. "1,3-5,11")
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

        If `text` and `id` are not given, pattern IDs will be automatically
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
        """Refresh aggregate remote information."""
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
        """msg_query [ptn_ids=ranges] [host_ids=ranges] [begin=timestamp]
                     [end=timestamp]

        Query messages per given conditions.
        """
        global service
        (kwargs, args) = (self.kwargs, self.args)
        ts0 = kwargs["begin"]
        if ts0:
            ts0 = ts0.sec
        ts1 = kwargs["end"]
        if ts1:
            ts1 = ts1.sec
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
            ("store", str, "3600-1", 0),
            ("ptn_ids", str, "RANGES", 0),
            ("host_ids", str, "RANGES", 0),
            ("begin", abhttp.Timestamp.fromStr, "TIMESTAMP", 0),
            ("end", abhttp.Timestamp.fromStr, "TIMESTAMP", 0),
        )

    def do_img_query(self, arg):
        """img_query [store=3600-1] [ptn_ids=ranges] [host_ids=ranges]
                     [begin=timestamp] [end=timestamp]

        Query messages per given conditions.
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

    def precmd(self, line):
        """Apply our parser and store results in self.kwargs, self.args."""
        tmp = line.split(None, 1)
        cmd = tmp[0]
        arg = "" if len(tmp) == 1 else tmp[1]
        p = self.get_parser(cmd)
        if p:
            (self.kwargs, self.args) = p.parse(arg)
            # output redirection
            out_mask = [x.startswith(">") for x in self.args]
            s = sum(out_mask)
            if s > 1:
                # too many redirections
                raise CmdException("Too many redirections")
            if s:
                # has one file redirection
                try:
                    idx = out_mask.index(True)
                    out = self.args.pop(idx)
                    out_name = out.strip(">")
                    if not out_name:
                        out_name = self.args.pop(idx)
                    if out.startswith(">>"):
                        out = open(out_name, "a")
                    else:
                        out = open(out_name, "w")
                except Exception, e:
                    raise CmdException(str(e))
            else:
                out = sys.stdout
            self.cmdout = out
        else:
            (self.kwargs, self.args) = ({}, [])
            self.cmdout = sys.stdout
        return line # return same line for Cmd processing.

    def postcmd(self, stop, line):
        if self.cmdout != self.stdout:
            self.cmdout.close()
            self.cmdout = self.stdout # reset cmdout
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
        else:
            # cmdloop exit cleanly, exit the program.
            break
    logger.debug("END OF PROGRAM.")

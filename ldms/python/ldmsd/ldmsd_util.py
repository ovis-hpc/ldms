#######################################################################
# -*- c-basic-offset: 8 -*-
# Copyright (c) 2015,2018 National Technology & Engineering Solutions
# of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
# NTESS, the U.S. Government retains certain rights in this software.
# Copyright (c) 2015,2018 Open Grid Computing, Inc. All rights reserved.
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

'''
"""
@module ovis_test_util
  Utility for OVIS test infrastructure
"""
from builtins import str
from builtins import object
import shlex
from subprocess import Popen, PIPE, STDOUT, DEVNULL
import re
import os
import time
import tempfile
import fcntl
import io
from io import StringIO
import errno
import tty
import shutil

def add_cmd_line_arg(arg, value = None):
    """Return a string of command line option and value

    @param   arg:   Command line option
    @param   value:   The value of the option. The default is None.

    @return:  Command line option value string. For example,
             if arg is '-w' and value is '32', the string '-w 32' is returned.
             If the value is None, the arg string is returned.
    """
    s = " {0}".format(arg)

    if value:
        s += " {0}".format(value)
    return s

def sh_exec(cmd):
    """Execute a shell command

        The function uses Popen to execute the given \c cmd.
        @param   cmd:   the shell command to be executed.

        @return:   The list [cmd return code, cmd stdout, cmd stderr]
    """
    try:
        sh_cmd = shlex.split(cmd)
        p = Popen(sh_cmd, stdout=PIPE, stderr=PIPE)
        output = p.communicate()
        return [p.returncode, output[0], output[1]]
    except Exception as e:
        if not e.args:
            e.args = ('',)
        e.args = e.args + ('sh_exec: cmd: ' + cmd, 'sh_exec: sh_cmd: ', sh_cmd)
        raise

def bash_exec(bash_cmd):
    """Execute bash command

    The function is equivalent to execute 'bash -c "<bash_cmd>"' on command-line.

    Example:
    bash_cmd is "rm -fr $PWD/*".
        The function will execute: bash -c "rm -fr $PWD/*".

    @param bash_cmd: Bash command

    @return: The list [cmd return code, cmd stdout, cmd stderr]

    @see: sh_exec
    """
    cmd = 'bash -c "{0}"'.format(bash_cmd)
    return sh_exec(cmd)

def ssh_exec(host, cmd, ssh_options = None):
    if not isinstance(host, str):
        raise TypeError("'host' must be a string")
    cmd_s = "ssh"
    if ssh_options is not None:
        for arg in ssh_options:
            cmd_s += add_cmd_line_arg(arg, ssh_options[arg])
    cmd_s += " '{0}' ".format(cmd)

    try:
        return sh_exec(cmd_s)
    except Exception as e:
        if not e.args:
            e.args = ('',)
        e.args = e.args + ('ssh_exec: cmd: ' + cmd_s)
        raise

def pdsh_exec(hosts_s, cmd, max_thr = 32, pdsh_options = None):
    """Execute a pdsh command

    The command is fed to the pdsh literally, covered by single quotes.

    @param hosts_s:     The hosts to execute the command \c cmd. The format is the
                        same as the value of the option '-w.'
    @param max_thr: Equivalent to the option '-f'
    @param pdsh_options: A dictionary in which keys are other pdsh options and
                        values are the option values. E.g., {'-t': 20}
    @param cmd:          The command

    @return: [pdsh returncode, pdsh stdout string, pdsh stderr string]
    """
    if not isinstance(hosts_s, str):
        raise TypeError("hosts_s must be a string, not {0}".format(type(hosts_s)))

    cmd_s = "pdsh"
    cmd_s += add_cmd_line_arg("-f", max_thr)
    cmd_s += add_cmd_line_arg("-w", hosts_s)
    if pdsh_options is not None:
        for key in pdsh_options:
            cmd_s += add_cmd_line_arg(key, pdsh_options[key])

    cmd_s += " '" + cmd + "'"

    try:
        return sh_exec(cmd_s)
    except Exception as e:
        if not e.args:
            e.args = ('',)
        e.args = e.args + ('pdsh_exec: cmd: ' + cmd_s, )
        raise

def parse_pdsh_exec_output(pdsh_output):
    """Parse the output of the pdsh_exec function

       The function returns the dictionary containing 'hostname': {'out': [], 'err': [], 'pdsh': []}.
       For each hostname, 'out' and 'err' are the lists of messages, including the hostname,
       returned to stdout and stderr from the \c cmd, respectively, and 'pdsh' is
       the pdsh return message for the particular host. A special key of the returned
       dictionary is 'other' which contains the messages that the hostname cannot be extracted.
       The messages of the 'other' key are the exact messages.

       @param   pdsh_output:   the returned list from the pdsh_exec() function.

       @return:   {'<hostname>|other': {'out': [], 'err': [], 'pdsh':[]}}
    """
    [ret, out, err] = pdsh_output
    host_output = {}

    ptn = re.compile(r"(?P<host>[^:]+): (?P<msg>.*)")
    pdsh_ptn = re.compile(r"(?P<junk>[^:]+): (?P<host>[^:]+): (?P<msg>.*)")

    #=====STDOUT=======
    out = out.split("\n")
    out.remove("")
    for line in out:
        #====cmd stdout
        _ptn = ptn
        _ptn_count = 2
        key = 'out'

        if line[0:4] == "pdsh":
            #====This is pdsh stdout
            _ptn = pdsh_ptn
            _ptn_count = 3
            key = 'pdsh'

        m = _ptn.match(line)
        if m is None:
            host = 'other'
            msg = line
        else:
            host = m.groupdict()['host']
            msg = m.groupdict()['msg']

        if host not in host_output:
            host_output[host] = {'out': [], 'err': [], 'pdsh': []}

        host_output[host][key].append(msg)

    #======STDERR========
    err = err.split("\n")
    err.remove("")
    for line in err:
        #====Default is the cmd stderr
        _ptn = ptn
        _ptn_count = 2
        key = 'err'

        if line[0:4] == "pdsh":
            #====This is pdsh stderr
            _ptn = pdsh_ptn
            _ptn_count = 3
            key = 'pdsh'

        m = _ptn.match(line)
        if m is None:
            host = 'other'
            msg = line
        else:
            host = m.groupdict()['host']
            msg = m.groupdict()['msg']

        if host not in host_output:
            host_output[host] = {'out': [], 'err': [], 'pdsh': []}

        host_output[host][key].append(msg)
    return host_output

def empty_dir(hosts, path):
    cmd = "rm -fr {0}/*".format(path)
    if hosts is None:
        bash_exec(cmd)
    else:
        remote_hosts = list(hosts)
        if "localhost" in remote_hosts:
            remote_hosts.remove("localhost")
            bash_exec(cmd)
            if len(remote_hosts) == 0:
                return
        pdsh_exec(hosts_s = ",".join(remote_hosts), cmd = cmd,
                  max_thr = len(remote_hosts), pdsh_options = {'-S': None})

def remove_file_cmd(filepath):
    """Construct the command   rm -f <filepath>

    @param filepath: Full path to a filepath

    @return: String "rm -f <filepath>"
    """
    if filepath is None:
        raise TypeError("sockfile must be string, not 'NoneType")

    return "rm -f {0}".format(filepath)

def remove_local_file(filepath):
    """Remove the filepath on the localhost

    @param filepath: Full path to the file to be removed.

    @return: [return code, stdout, stderr]

    @see: remove_remote_file, remove_file
    """
    return bash_exec(remove_file_cmd(filepath))

def remove_remote_file(hosts, filepath):
    """Remove the filepath on all hosts

    @param hosts: List of hosts
    @param filepath: Full path to the file to be removed.

    @return: [return code, stdout, stderr]

    @see: remove_remote_file, remove_file
    """
    return pdsh_exec(hosts_s = ",".join(hosts), cmd = remove_file_cmd(filepath),
                     max_thr = len(hosts), pdsh_options = {'-S': None})

def remove_file(hosts, filepath):
    """Remove the filepath

    The command to kill the ldmsd process is generated by kill_9_ldmsd_cmd.

    @param hosts: List of hosts or None
    @param filepath: Full path to the file to be removed.

    @return: [largest return code, pdsh stdout, pdsh stderr] if hosts is not None.
             Otherwise, [return code, stdout, stderr] of the rm -f command executed
             on the localhost.

    @see: remove_local_file, remove_remote_file, remove_file_cmd
    """
    if hosts is None:
        remove_local_file(filepath)
    else:
        remote_hosts = list(hosts)
        if "localhost" in remote_hosts:
            remote_hosts.remove("localhost")
            remove_local_file(filepath)
            if len(remote_hosts) == 0:
                return
        remove_remote_file(hosts, filepath)

def get_var_from_file(module_name, filepath):
    """Import a python file containing variables and their values

    @param module_name: Module name
    @param filepath: The full path to a file

    @return: A module of the name module_name

    @see: imp.load_source
    """
    import imp
    f = open(filepath)
    data = imp.load_source(module_name, '', f)
    f.close()
    return data

class LDMSD(object):
    """A utility class to handle an LDMS Daemon subprocess"""

    def __init__(self, port, xprt="sock", logfile=None, auth="none",
                 auth_opt={}, verbose="INFO", cfg=None, host_name=None,
                 gdb_port=None, name=None):
        """LDMSD subprocess handler initialization

        @param port(str): the LDMSD listening port.
        @param xprt(str): the transport type ("sock", "rdma", or "ugni").
        @param logfile(str): the path to the logfile. If the value is `None`,
                             no log is produced.
        @param auth(str): the name of the LDMS authentication plugin.
        @param auth_opt(dict): the dictionary of key-value specifying
                               authentication plugin options.
        @param verbose(str): the verbosity of the log.
        @param cfg(str): the daemon configuration.
        @param host_name(str): the daemon hostname.
        @param gdb_port(str): the port of the gdbserver. If this is `None`, the
                              process will NOT be under gdb. If the port is
                              specified, the process will run under gdbserver.
        """
        self.cmd_args = []
        if gdb_port:
            ldmsd_path = shutil.which("ldmsd")
            if not ldmsd_path:
                raise RuntimeError("ldmsd not found")
            self.cmd_args.extend([
                    "gdbserver",
                    "localhost:%s" % str(gdb_port),
                    ldmsd_path,
                ])
        else:
            self.cmd_args.append("ldmsd")
        self.cmd_args.extend([
            "-F", # foreground
            "-x", "%s:%s" % (xprt, port),
            "-a", auth,
            "-v", verbose,
        ])
        if logfile:
            self.cmd_args.extend(["-l", logfile])
        if auth_opt:
            for a,v in auth_opt.items():
                self.cmd_args.extend(["-A", "%s=%s" % (a, v)])
        if host_name:
            self.cmd_args.extend(["-H", host_name])
        if name:
            self.cmd_args.extend(["-n", name])
        self.proc = None
        self.cfg = None
        if cfg:
            self.cfg = tempfile.NamedTemporaryFile(mode='w', encoding='utf-8')
            self.cfg.write(cfg)
            self.cfg.flush()
            self.cmd_args.extend(["-c", self.cfg.name])

    def is_running(self):
        """Return `True` if the daemon is running"""
        if not self.proc:
            return False
        self.proc.poll()
        #return self.proc.returncode == 0
        if self.proc.returncode == 0 or self.proc.returncode == None:
            return True
        else:
            return False

    def run(self):
        """Run the daemon"""
        if self.proc:
            raise RuntimeError("LDMSD already running")
        self.proc = Popen(self.cmd_args,
                          stdin=DEVNULL,
                          stdout=DEVNULL,
                          stderr=DEVNULL,
                          close_fds = True,
                          )
        time.sleep(0.01)
        self.proc.poll()
        if self.proc.returncode != None:
            return self.proc.returncode

    def term(self):
        """Terminate the daemon"""
        if self.proc:
            self.proc.terminate()
            # also drain the output
            self.proc.communicate()
            self.proc = None

    def __del__(self):
        self.term()
        if self.cfg:
            self.cfg.close()

    def __repr__(self):
        return """<LDMSD %s>""" % " ".join(self.cmd_args)

class LDMSD_Controller(object):
    """ldmsd_controller subprocess handler"""

    def __init__(self, port, host="localhost", xprt="sock",
                       auth="none", auth_opt={},
                       source=None, script=None,
                       ldmsctl=False):
        """LDMSD_Controller initialization

        @param port(str): the port of ldmsd to connect to.
        @param host(str): the hostname hosting the ldmsd.
        @param auth(str): the name of the authentication plugin
        @param auth_opt(dict): the dictionary of key-value specifying
                               authentication plugin options.
        """
        self.term_immediately = False
        if source is not None or script is not None:
            self.term_immediately = True
        self.is_ldmsctl = ldmsctl
        if self.is_ldmsctl:
            self.cmd_args = [
                "exec ldmsctl",
                "-h", host,
                "-p", str(port),
                "-x", xprt,
                "-a", auth,
            ]
            if source is not None:
                self.cmd_args.extend(["-s", source])
            if script is not None:
                self.cmd_args.extend(["-X", script])
        else:
            self.cmd_args = [
                "exec ldmsd_controller",
                "--host", host,
                "--port", str(port),
                "--xprt", xprt,
                "-a", auth,
            ]
            if source is not None:
                self.cmd_args.extend(["--source", source])
            if script is not None:
                self.cmd_args.extend(["--script", script])
        for a, v in auth_opt.items():
            self.cmd_args.extend(["-A", "%s=%s" % (a, v)])
        self.proc = None
        self.pty = None

    def is_running(self):
        """Return `True` if the subprocess is running"""
        if not self.proc:
            return False
        self.proc.poll()
        if self.proc.returncode == 0 or self.proc.returncode == None:
            return True
        else:
            return False
    def run(self):
        """Run ldmsd_controller subprocess"""
        if self.proc:
            raise RuntimeError("process already running")
        # Create pty
        _mfd, _sfd = os.openpty()
        fl = fcntl.fcntl(_mfd, fcntl.F_GETFL)
        rc = fcntl.fcntl(_mfd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
        assert(rc == 0)
        self.pty = os.fdopen(_mfd, "r+b", 0)
        tty.setraw(_sfd) # set RAW mode
        cmd = " ".join(self.cmd_args)
        self.proc = Popen(cmd,
                          stdin=_sfd,
                          stdout=_sfd,
                          stderr=STDOUT,
                          close_fds = True,
                          shell = True
                          )
        if self.term_immediately:
            return
        time.sleep(0.5)
        if not self.is_running():
            raise RuntimeError("process terminated prematurely")

    def term(self):
        """Terminate the ldmsd_controller subprocess"""
        if not self.proc:
            raise RuntimeError("process not running")
        self.proc.terminate()
        # also drain the output
        self.proc.communicate()
        self.proc = None

    def read_pty(self, dt = 0.5, n = 2):
        """Read the pty until idle

        Idle means the pty.read() got EAGAIN for `n` times consecutively with
        interval `dt` seconds.
        """
        if not self.proc:
            raise RuntimeError("process not running")
        _c = 0 # initialize counter
        _out = StringIO()
        while True:
            try:
                _s = self.pty.read()
                if _s is not None:
                    _out.write(_s.decode())
                    _c = 0 # reset counter
                else:
                    raise IOError(errno.EAGAIN, 'no data, try again')
            except IOError as e:
                if e.errno != errno.EAGAIN:
                    raise
                _c += 1
                if _c == n:
                    break
                time.sleep(dt)
        return _out.getvalue()

    def write_pty(self, s):
        """Write string `s` to the ldmsd_controller pty"""
        if not self.proc:
            raise RuntimeError("process not running")
        self.pty.write(s.encode())

    def comm_pty(self, cmd):
        """Write `cmd` to pty and Read repsonse from the pty"""
        _cmd = cmd.strip() + "\n"
        self.write_pty(_cmd)
        resp = self.read_pty()
        return resp.splitlines()

    def __del__(self):
        if self.proc:
            self.term()
        if self.pty:
            self.pty.close()

    def __repr__(self):
        return """<LDMSD_Controller %s>""" % " ".join(self.cmd_args)

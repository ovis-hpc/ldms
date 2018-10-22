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
Created on Apr 9, 2015

'''
from exceptions import TypeError
from ldmsd.ldmsd_util import bash_exec
import socket

"""
@module ldmsd_test_setup

This module provides APIs to setup ldmsd test environment.

NOTE: Most features provided in the module come with three APIs: *_local_<feature>*,
      *_remote_<feature>* and *_<feature>. The three APIs serve the same
      functionality but on different host.

      The 'local' APIs are for localhost, no network communication involved.

      On the other hand, the 'remote' APIs are for multiple hosts. They require
      the list of hosts to take affect on. If the APIs are expected to return
      a value back, they return the dictionary of hosts and
      the result of each host. The pdsh command is used to parallely execute
      shell command. They return the dictionary of the hosts and the result from each host.

      Lastly the generic APIs without the word 'local' and 'remote'
      in their names are wrapper of the local and remote APIs. Similar to
      the remote APIs, they require the 'hosts' parameter and return the
      dictionary of hosts and the results. However, the value of the 'hosts'
      parameter could be None. In this case, they will in turn all the corresponding
      local APIs and return the dictionary of one element containing
      the 'localhost' key and its result.

"""
from ldmsd.ldmsd_util import add_cmd_line_arg, sh_exec, pdsh_exec, parse_pdsh_exec_output


def get_test_instance_name(hostname, xprt, port, prefix_name, set_no):
    if hostname == "localhost":
        hostname = socket.gethostname()
    return "{0}_{1}_{2}/{3}_{4}".format(hostname, xprt, port, prefix_name, set_no)

"""@var ldmsd_arg_map
  The map between the package specific arguments and the ldmsd command line options.
"""
ldmsd_arg_map = {'xprt_port' : "-x",
                  'log' : "-l",
                  'sock' : "-S",
                  'ocm_port' : "-o",
                  'mem_size' : "-m",
                  'ldmsd_mode' : "-z",
                  'foreground' : "-F",
                  'verbose' : "-v",
                  'num_ethreads' : "-P",
                  'num_fthreads' : "-f",
                  'dirty_threshold' : "-D",
                  'publish_kernel_metrics' : "-k",
                  'setfile' : "-s",
                  'test_sample_interval' : "-i",
                  'test_set_count' : "-t",
                  'test_set_name' : "-T",
                  'test_metric_count': "-M",
                  'test_notify' : "-N",
                  'inet_ctrl_port': "-p",
                  'rctrl_listener_port': "-r"}

def start_ldmsd_cmd(xprt, port, log = None,
                    sock = None, ocm_port = None,
                    mem_size = None, ldmsd_mode = None,
                    foreground = False, verbose = None,
                    num_ethreads = None, num_fthreads = None,
                    dirty_threshold = None,
                    publish_kernel_metrics = False, setfile = None,
                    test_sample_interval = None, test_set_count = None,
                    test_set_name = None, test_metric_count = None,
                    test_notify = None,
                    inet_ctrl_port = None,
                    rctrl_listener_port = None):
    """ Return the command line to start an ldmsd process according to the given options.

    Note that the option '-x', equivalently the key 'xprt_port' in ldmsd_arg_map,
    is divided into the arguments xprt and port. The other arguments are
    can be mapped to an ldmsd command line option.

    @return:   String to start an ldmsd process
    """
    if xprt is None:
        raise TypeError("xprt must be string, not 'NoneType")

    if port is None:
        raise TypeError("port must be number string or number, not 'NoneType'")

    cmd = "ldmsd"
    if (xprt and port):
        cmd += add_cmd_line_arg(ldmsd_arg_map['xprt_port'],
                                    "{0}:{1}".format(xprt, port))
    if (log):
        cmd += add_cmd_line_arg(ldmsd_arg_map['log'], log)
    if (sock):
        cmd += add_cmd_line_arg(ldmsd_arg_map['sock'], sock)
    if (mem_size):
        cmd += add_cmd_line_arg(ldmsd_arg_map['mem_size'], mem_size)
    if (ocm_port):
        cmd += add_cmd_line_arg(ldmsd_arg_map['ocm_port'], ocm_port)
    if (foreground):
        cmd += add_cmd_line_arg(ldmsd_arg_map['foreground'])
    if (verbose):
        cmd += add_cmd_line_arg(ldmsd_arg_map['verbose'], verbose)
    if (num_ethreads):
        cmd += add_cmd_line_arg(ldmsd_arg_map['num_ethreads'], num_ethreads)
    if (num_fthreads):
        cmd += add_cmd_line_arg(ldmsd_arg_map['num_fthreads'], num_fthreads)
    if (dirty_threshold):
        cmd += add_cmd_line_arg(ldmsd_arg_map['dirty_threshold'], dirty_threshold)
    if (ldmsd_mode):
        cmd += add_cmd_line_arg(ldmsd_arg_map['ldmsd_mode'], ldmsd_mode)
    if (publish_kernel_metrics):
        cmd += add_cmd_line_arg(ldmsd_arg_map['publish_kernel_metrics'])
    if (setfile):
        cmd += add_cmd_line_arg(ldmsd_arg_map['setfile'])
    if (test_sample_interval):
        cmd += add_cmd_line_arg(ldmsd_arg_map['test_sampler_interval'], test_sample_interval)
    if (test_set_name):
        cmd += add_cmd_line_arg(ldmsd_arg_map['test_set_name'], test_set_name)
    if (test_notify):
        cmd += add_cmd_line_arg(ldmsd_arg_map['test_notify'])
    if (test_set_count):
        cmd += add_cmd_line_arg(ldmsd_arg_map['test_set_count'], test_set_count)
    if test_metric_count:
        cmd += add_cmd_line_arg(ldmsd_arg_map['test_metric_count'], test_metric_count)
    if (inet_ctrl_port):
        cmd += add_cmd_line_arg(ldmsd_arg_map['inet_ctrl_port'], inet_ctrl_port)
    if (rctrl_listener_port):
        cmd += add_cmd_line_arg(ldmsd_arg_map['rctrl_listener_port'], rctrl_listener_port)
    return cmd

def ldmsd_pid_cmd(xprt, port):
    """Return the command to get the pid of an ldmsd

    @return: Command string <tt>pgrep -f 'ldmsd.+<xprt>:<port>'</tt>
    """
    if xprt is None:
        raise TypeError("xprt must be string, not 'NoneType")

    if port is None:
        raise TypeError("port must be number string or number, not 'NoneType'")

    return "pgrep -f ^ldmsd.+{0}:{1}".format(xprt, port)

def get_local_ldmsd_pid(xprt, port):
    """Get pid of the ldmsd using xprt and listening on port

     The function gets the pid by calling 'pgrep -f 'ldmsd.+<xprt>:<port>'.

     @param   xprt:   transport the ldmsd uses
     @param   port:   ldmsd listener port

     @return: pid on success or -1 if pgrep returns nothing
    """
    output = sh_exec(ldmsd_pid_cmd(xprt, port))
    if len(output[1]) > 0:
        return output[1].rstrip().split("\n")
    else:
        return []

def get_remote_ldmsd_pid(hosts, xprt, port):
    """ Get the ldmsd pid on the remote hosts hosts

    pdsh is used to execute the command returned from ldmsd_pid_cmd().

    @param  hosts:   List of remote hosts
    @param  xprt:    ldmsd xprt
    @param  port:    ldmsd listener port

    @return: The dictionary of hosts and their pids {hostname: pid}. If it fails to get
            the pid from a host, the pid value is -1.
"""
    output = pdsh_exec(hosts_s = ",".join(hosts),
                      cmd = ldmsd_pid_cmd(xprt, port),
                      max_thr = len(hosts),
                      pdsh_options = {'-S': None})

    poutput = parse_pdsh_exec_output(output)
    pids = {}
    for host in hosts:
        if (host not in poutput) or (len(poutput[host]['out']) < 1):
            pids[host] = []
        else:
            pids[host] = poutput[host]['out']
    return pids

def get_ldmsd_pid(hosts, xprt, port):
    """ Get the pid of ldmsd using xprt on port

    @param  hosts:   List of hosts ldmsd processes running on. If hosts is None,
                     the ldmsd pid on the localhost will be returned.
    @param  xprt:    ldmsd xprt
    @param  port:    ldmsd listener port

    @return:  The dictionary of hosts and their pids, if hosts is not None.
              Otherwise, the dictionary containing only the localhost result is returned.
    """
    if isinstance(hosts, basestring):
        #===== hosts is an instance of either str or unicode
        if hosts == "localhost":
            hosts = None
        else:
            hosts = [hosts]

    if hosts is None:
        return {'localhost': get_local_ldmsd_pid(xprt, port)}
    else:
        ret = {}
        remote_hosts = list(hosts)
        if "localhost" in remote_hosts:
            remote_hosts.remove("localhost")
            ret = {'localhost': get_local_ldmsd_pid(xprt, port)}
            if len(remote_hosts) == 0:
                return ret
        ret.update(get_remote_ldmsd_pid(remote_hosts, xprt, port))
        return ret

def is_local_ldmsd_running(xprt, port):
    """Check if the ldmsd on the localhost is running

    The function checks if there are any pid of an ldmsd using the xprt
    and listening on the port

    @param  xprt:   ldmsd transport
    @param  port:   ldmsd listener port

    @return:   True if the pid is found. Otherwise, False is returned.

    @see: get_local_ldmsd_pid
    """
    pid = get_local_ldmsd_pid(xprt, port)
    if len(pid) <= 0:
        return False
    return True

def is_remote_ldmsd_running(hosts, xprt, port):
    """Check if the ldmsd on the hosts are running

    The function checks if there are any pid of an ldmsd using the xprt
    and listening on the port

    @param  hosts:  List of hosts ldmsd processes running on.
    @param  xprt:   ldmsd transport
    @param  port:   ldmsd listener port

    @return:   Dictionary {host: True/False}. True if the pid is found.
               Otherwise, False is returned.

    @see:  get_remote_ldmsd_pid
    """
    pid = get_remote_ldmsd_pid(hosts, xprt, port)
    out = {}
    for host in hosts:
        if len(pid[host]) <= 0:
            out[host] = False
        else:
            out[host] = True
    return out

def is_ldmsd_running(hosts, xprt, port):
    """Check if the ldmsd is running

    The function checks if there are any pid of an ldmsd using the xprt
    and listening on the port

    @param  hosts:  List of hosts ldmsd processes running on. If None, it will check on the localhost.
    @param  xprt:   ldmsd transport
    @param  port:   ldmsd listener port

    @return:   The dictionary of hosts and the results is returned. If
               the hosts is None, the dictionary contains only the localhost key.

    @see:  is_local_ldmsd_running, is_remote_ldmsd_running
    """
    if isinstance(hosts, basestring):
        #===== hosts is an instance of either str or unicode
        if hosts == "localhost":
            hosts = None
        else:
            hosts = [hosts]

    if hosts is None:
        return {'localhost' : is_local_ldmsd_running(xprt, port)}
    else:
        ret = {}
        remote_hosts = list(hosts)
        if "localhost" in remote_hosts:
            remote_hosts.remove("localhost")
            ret = {'localhost': is_local_ldmsd_running(xprt, port)}
            if len(remote_hosts) == 0:
                return ret
        ret.update(is_remote_ldmsd_running(remote_hosts, xprt, port))
        return ret

def start_local_ldmsd(xprt, port, log = None,
                    sock = None, ocm_port = None,
                    mem_size = None, ldmsd_mode = None,
                    foreground = False, verbose = None,
                    num_ethreads = None, num_fthreads = None,
                    dirty_threshold = None,
                    publish_kernel_metrics = False, setfile = None,
                    test_sample_interval = None, test_set_count = None,
                    test_set_name = None, test_metric_count = None,
                    test_notify = None,
                    inet_ctrl_port = None,
                    rctrl_listener_port = None):
    """ Start an ldmsd on the localhost

    @param  xprt:   ldmsd transport
    @param  port:   ldmsd listener port
    The other parameters are the ldmsd command-line options besides '-x'.

    @return: [ldmsd return code, stdout, stderr]

    @see: start_remote_ldmsd, start_ldmsd
    """
    start_cmd = start_ldmsd_cmd(xprt = xprt, port = port,
                                log = log, sock = sock,
                                ocm_port = ocm_port, mem_size = mem_size,
                                ldmsd_mode = ldmsd_mode,
                                foreground = foreground,
                                verbose = verbose, num_ethreads = num_ethreads,
                                num_fthreads = num_fthreads,
                                dirty_threshold = dirty_threshold,
                                publish_kernel_metrics = publish_kernel_metrics,
                                setfile = setfile,
                                test_sample_interval = test_sample_interval,
                                test_set_count = test_set_count,
                                test_set_name = test_set_name,
                                test_notify = test_notify,
                                inet_ctrl_port = inet_ctrl_port,
                                rctrl_listener_port = rctrl_listener_port)
    return bash_exec(start_cmd)

def start_remote_ldmsd(hosts, xprt, port, log = None,
                    sock = None, ocm_port = None,
                    mem_size = None, ldmsd_mode = None,
                    foreground = False, verbose = None,
                    num_ethreads = None, num_fthreads = None,
                    dirty_threshold = None,
                    publish_kernel_metrics = False, setfile = None,
                    test_sample_interval = None, test_set_count = None,
                    test_set_name = None, test_metric_count = None,
                    test_notify = None,
                    inet_ctrl_port = None,
                    rctrl_listener_port = None):
    """ Start an ldmsd on the hosts

    @param  hosts:  List of hosts
    @param  xprt:   ldmsd transport
    @param  port:   ldmsd listener port
    The other parameters are the ldmsd command-line options besides '-x'.

    @return: [largest return code of the ldmsd command among hosts, pdsh stdout, pdsh stderr]

    @see: start_local_ldmsd, start_ldmsd, parse_pdsh_exec_output
    """
    start_cmd = start_ldmsd_cmd(xprt = xprt, port = port,
                                log = log, sock = sock,
                                ocm_port = ocm_port, mem_size = mem_size,
                                ldmsd_mode = ldmsd_mode,
                                foreground = foreground,
                                verbose = verbose, num_ethreads = num_ethreads,
                                num_fthreads = num_fthreads,
                                dirty_threshold = dirty_threshold,
                                publish_kernel_metrics = publish_kernel_metrics,
                                setfile = setfile,
                                test_sample_interval = test_sample_interval,
                                test_set_count = test_set_count,
                                test_set_name = test_set_name,
                                test_metric_count = test_metric_count,
                                test_notify = test_notify,
                                inet_ctrl_port = inet_ctrl_port,
                                rctrl_listener_port = rctrl_listener_port)
    output = pdsh_exec(",".join(hosts), start_cmd, len(hosts), pdsh_options = {'-S': None})
    return output[0]

def start_ldmsd(hosts, xprt, port, log = None,
                    sock = None, ocm_port = None,
                    mem_size = None, ldmsd_mode = None,
                    foreground = False, verbose = None,
                    num_ethreads = None, num_fthreads = None,
                    dirty_threshold = None,
                    publish_kernel_metrics = False, setfile = None,
                    test_sample_interval = None, test_set_count = None,
                    test_set_name = None, test_metric_count = None,
                    test_notify = None,
                    inet_ctrl_port = None,
                    rctrl_listener_port = None):
    """ Start an ldmsd process on the localhost or remote hosts

    An ldmsd process is started on the localhost if hosts is None.
    Otherwise, an ldmsd process is started on each host in the hosts list.
    In this case the function uses pdsh to start the ldmsd on remote hosts.

    @param  hosts:  List of hosts to start an ldmsd process or None.
    @param  xprt:   ldmsd transport
    @param  port:   ldmsd listener port
    The other parameters are the ldmsd command-line options besides '-x'.

    @return:  [ largest return value of the ldmsd command, stdout, stderr]

    @see: start_local_ldmsd, start_remote_ldmsd, parse_pdsh_exec_output
    """
    if isinstance(hosts, basestring):
        #===== hosts is an instance of either str or unicode
        if hosts == "localhost":
            hosts = None
        else:
            hosts = [hosts]

    if hosts is None:
        return start_local_ldmsd(xprt = xprt, port = port,
                                log = log, sock = sock,
                                ocm_port = ocm_port, mem_size = mem_size,
                                ldmsd_mode = ldmsd_mode,
                                foreground = foreground,
                                verbose = verbose, num_ethreads = num_ethreads,
                                num_fthreads = num_fthreads,
                                dirty_threshold = dirty_threshold,
                                publish_kernel_metrics = publish_kernel_metrics,
                                setfile = setfile,
                                test_sample_interval = test_sample_interval,
                                test_set_count = test_set_count,
                                test_set_name = test_set_name,
                                test_metric_count = test_metric_count,
                                test_notify = test_notify,
                                inet_ctrl_port = inet_ctrl_port,
                                rctrl_listener_port = rctrl_listener_port)
    else:
        ret = 0
        remote_hosts = list(hosts)
        if "localhost" in remote_hosts:
            remote_hosts.remove("localhost")
            ret = start_local_ldmsd(xprt = xprt, port = port,
                                log = log, sock = sock,
                                ocm_port = ocm_port, mem_size = mem_size,
                                ldmsd_mode = ldmsd_mode,
                                foreground = foreground,
                                verbose = verbose, num_ethreads = num_ethreads,
                                num_fthreads = num_fthreads,
                                dirty_threshold = dirty_threshold,
                                publish_kernel_metrics = publish_kernel_metrics,
                                setfile = setfile,
                                test_sample_interval = test_sample_interval,
                                test_set_count = test_set_count,
                                test_set_name = test_set_name,
                                test_metric_count = test_metric_count,
                                test_notify = test_notify,
                                inet_ctrl_port = inet_ctrl_port,
                                rctrl_listener_port = rctrl_listener_port)
            if len(remote_hosts) == 0:
                return ret
        ret_ = start_remote_ldmsd(hosts = remote_hosts, xprt = xprt, port = port,
                                log = log, sock = sock,
                                ocm_port = ocm_port, mem_size = mem_size,
                                ldmsd_mode = ldmsd_mode,
                                foreground = foreground,
                                verbose = verbose, num_ethreads = num_ethreads,
                                num_fthreads = num_fthreads,
                                dirty_threshold = dirty_threshold,
                                publish_kernel_metrics = publish_kernel_metrics,
                                setfile = setfile,
                                test_sample_interval = test_sample_interval,
                                test_set_count = test_set_count,
                                test_set_name = test_set_name,
                                test_metric_count = test_metric_count,
                                test_notify = test_notify,
                                inet_ctrl_port = inet_ctrl_port,
                                rctrl_listener_port = rctrl_listener_port)
        ret = max([ret, ret_])
        return ret

def kill_ldmsd_cmd(xprt, port):
    """Construct the command   pkill -f ^ldmsd.+<xprt>:<port>

    @param xprt: ldmsd transport
    @param port: ldmsd listener port

    @return: String "pkill -f ^ldmsd.+<xprt>:<port>"
    """
    if xprt is None:
        raise TypeError("xprt must be string, not 'NoneType")

    if port is None:
        raise TypeError("port must be number string or number, not 'NoneType'")

    return "pkill -f ^ldmsd.+{0}:{1}".format(xprt, port)

def kill_local_ldmsd(xprt, port):
    """Kill the ldmsd process on the localhost.

    The command to kill the ldmsd process is generated by kill_ldmsd_cmd.

    @param xprt: transport of the ldmsd to be killed
    @param port: listener port of the ldmsd to be killed.

    @return: [return code, stdout, stderr]

    @see: kill_ldmsd_cmd, kill_remote_ldmsd, kill_ldmsd
    """
    return sh_exec(kill_ldmsd_cmd(xprt, port))

def kill_remote_ldmsd(hosts, xprt, port):
    """Kill the ldmsd processes on the hosts

    The command to kill the ldmsd process is generated by kill_ldmsd_cmd.

    @param hosts: List of hosts
    @param xprt: ldmsd transport
    @param port: ldmsd listener port

    @return: [largest return code, pdsh stdout, pdsh stderr]

    @see: kill_ldmsd_cmd, kill_local_ldmsd, kill_ldmsd
    """
    return pdsh_exec(hosts_s = ",".join(hosts), cmd = kill_ldmsd_cmd(xprt, port),
                     max_thr = len(hosts), pdsh_options = {'-S': None})

def kill_ldmsd(hosts, xprt, port):
    """Kill the ldmsd processes

    The command to kill the ldmsd process is generated by kill_ldmsd_cmd.

    @param hosts: List of hosts or None
    @param xprt: ldmsd transport
    @param port: ldmsd listener port

    @return: [largest return code, pdsh stdout, pdsh stderr] if hosts is not None.
             Otherwise, [return code, stdout, stderr] of the kill command executed
             on the localhost.

    @see: kill_ldmsd_cmd, kill_local_ldmsd, kill_remote_ldmsd
    """
    if isinstance(hosts, basestring):
        #===== hosts is an instance of either str or unicode
        if hosts == "localhost":
            hosts = None
        else:
            hosts = [hosts]

    if hosts is None:
        return kill_local_ldmsd(xprt, port)
    else:
        ret = 0
        remote_hosts = list(hosts)
        if "localhost" in remote_hosts:
            remote_hosts.remove("localhost")
            ret = kill_local_ldmsd(xprt, port)
            if len(remote_hosts) == 0:
                return ret
        ret_ = kill_remote_ldmsd(remote_hosts, xprt, port)
        ret = max([ret, ret_])
        return ret

def kill_9_ldmsd_cmd(xprt, port):
    """Construct the command   pkill -9 -f ^ldmsd.+<xprt>:<port>

    @param xprt: ldmsd transport
    @param port: ldmsd listener port

    @return: String "pkill -9 -f ^ldmsd.+<xprt>:<port>"
    """
    if xprt is None:
        raise TypeError("xprt must be string, not 'NoneType")

    if port is None:
        raise TypeError("port must be number string or number, not 'NoneType'")
    return "pkill -9 -f ^ldmsd.+{0}:{1}".format(xprt, port)

def kill_9_local_ldmsd(xprt, port):
    """Kill the ldmsd process on the localhost using kill -9.

    The command to kill the ldmsd process is generated by kill_9_ldmsd_cmd.

    @param xprt: transport of the ldmsd to be killed
    @param port: listener port of the ldmsd to be killed.

    @return: [return code, stdout, stderr]

    @see: kill_9_ldmsd_cmd, kill_local_ldmsd
    """
    return sh_exec(kill_9_ldmsd_cmd(xprt, port))

def kill_9_remote_ldmsd(hosts, xprt, port):
    """Kill the ldmsd processes on the hosts

    The command to kill the ldmsd process is generated by kill_9_ldmsd_cmd.

    @param hosts: List of hosts
    @param xprt: ldmsd transport
    @param port: ldmsd listener port

    @return: [largest return code, pdsh stdout, pdsh stderr]

    @see: kill_9_ldmsd_cmd, kill_remote_ldmsd
    """
    return pdsh_exec(hosts_s = ",".join(hosts), cmd = kill_9_ldmsd_cmd(xprt, port),
                     max_thr = len(hosts), pdsh_options = {'-S': None})

def kill_9_ldmsd(hosts, xprt, port):
    """Kill the ldmsd processes using kill -9

    The command to kill the ldmsd process is generated by kill_9_ldmsd_cmd.

    @param hosts: List of hosts or None
    @param xprt: ldmsd transport
    @param port: ldmsd listener port

    @return: [largest return code, pdsh stdout, pdsh stderr] if hosts is not None.
             Otherwise, [return code, stdout, stderr] of the kill command executed
             on the localhost.

    @see: kill_9_local_ldmsd, kill_9_remote_ldmsd, kill_ldmsd
    """
    if isinstance(hosts, basestring):
        #===== hosts is an instance of either str or unicode
        if hosts == "localhost":
            hosts = None
        else:
            hosts = [hosts]

    if hosts is None:
        return kill_9_local_ldmsd(xprt, port)
    else:
        ret = 0
        remote_hosts = list(hosts)
        if "localhost" in remote_hosts:
            remote_hosts.remove("localhost")
            ret = kill_9_local_ldmsd(xprt, port)
            if len(remote_hosts) == 0:
                return ret
        ret_ = kill_9_remote_ldmsd(remote_hosts, xprt, port)
        ret = max([ret, ret_])
        return ret

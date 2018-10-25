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
from time import sleep
'''
Created on Jun 1, 2015

'''

from ovis_ldms import ldms
from ldmsd.ldmsd_setup import kill_ldmsd, is_ldmsd_running, kill_9_ldmsd,\
    start_ldmsd
from ldmsd.ldmsd_util import remove_file, get_var_from_file


def sec2microsec(sec):
    return sec * 1000000

def microsec2sec(ms):
    return ms / 1000000;

def load_test_config(path):
    cfg = get_var_from_file("cfg", path)
    cfg.LDMSD_RECONNECT_INTERVAL = sec2microsec(cfg.LDMSD_RECONNECT_INTERVAL)
    cfg.LDMSD_UPDATE_INTERVAL = sec2microsec(cfg.LDMSD_UPDATE_INTERVAL)
    return cfg

def ldms_connect(log, host, xprt, port):
    log.debug("---ldms_conn---")
    x = ldms.ldms_xprt_new(xprt, None)
    rc = ldms.ldms_xprt_connect_by_name(x, host, str(port), None, None)
    if rc != 0:
        raise Exception("ldms_xprt_connect_by_name failed to {0}:{1}".format(host, port))
    log.debug("---ldms_conn DONE---")
    return x

def stop_test_ldmsds(hosts, xprt, port, sock, log):
    kill_ldmsd(hosts = hosts, xprt = xprt, port = port)
    is_running = is_ldmsd_running(hosts, xprt, port)
    not_died = filter(lambda host: is_running[host], is_running)
    if len(not_died) > 0:
        log.debug("---- kill 9 samplerd: {0}".format(",".join(not_died)))
        kill_9_ldmsd(hosts = not_died, xprt = xprt, port = port)
    remove_file(hosts = hosts, filepath = sock)

def start_test_ldmsds(**kwargs):
    hosts = kwargs['hosts']
    xprt = kwargs['xprt']
    port = kwargs['port']

    start_ldmsd(**kwargs)
    sleep(1)
    is_running = is_ldmsd_running(hosts = hosts, xprt = xprt, port = port)
    not_started = filter(lambda host: not is_running[host], is_running)
    if len(not_started) > 0:
        raise Exception("Failed to start ldmsd_samplerd on " + ",".join(not_started))

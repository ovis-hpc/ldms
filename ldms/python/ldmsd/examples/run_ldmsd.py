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
Created on May 28, 2015

'''
import sys
import traceback
from argparse import ArgumentParser
from ldmsd.ldmsd_setup import start_ldmsd, kill_ldmsd, is_ldmsd_running,\
    get_ldmsd_pid, kill_9_ldmsd
from ldmsd.ldmsd_util import empty_dir, get_var_from_file, remove_file

def main(argv = None):
    if argv is None:
        argv = sys.argv
    else:
        sys.argv.extend(argv)

    try:
        parser = ArgumentParser()
        parser.add_argument('--config-file', help = "Path to the config file", required = True)
        parser.add_argument('--start-samplerd', help = "start samplerd ldmsd",
                            action = "store_true")
        parser.add_argument('--kill-samplerd', action = "store_true",
                            help = "kill samplerd ldmsd")
        parser.add_argument('--start-agg', action = "store_true",
                            help = "start agg ldmsd")
        parser.add_argument('--kill-agg', action = "store_true",
                            help = "kill agg ldmsd")
        parser.add_argument('--kill-9-samplerd', action = "store_true",
                            help = "kill 9 samplerd ldmsd")
        parser.add_argument('--kill-9-agg', action = "store_true",
                            help = "kill 9 agg ldmsd")
        parser.add_argument('--remove-samplerd-files', action = "store_true",
                            help = "Remove the samplerd's log and sock files")
        parser.add_argument('--remove-agg-files', action = "store_true",
                            help = "Remove the aggregataor(s)'s log and sock files")
        parser.add_argument('--check-samplerd', action = "store_true",
                            help = "Check samplerd ldmsd running")
        parser.add_argument('--check-agg', action = "store_true",
                            help = "Check agg ldmsd running")
        parser.add_argument('--samplerd-pid', help = "Get the samplerd PIDs",
                            action = "store_true")
        parser.add_argument('--agg-pid', help = "Get the agg PIDs",
                            action = "store_true")
        args = parser.parse_args()

        cfg = get_var_from_file(module_name = "cfg", filepath = args.config_file)

        if args.start_samplerd:
            print "start samplerd.."
            start_ldmsd(hosts = cfg.SAMPLERD_HOSTS,
                               xprt = cfg.SAMPLERD_XPRT, port = cfg.SAMPLERD_PORT,
                               log = cfg.SAMPLERD_LOG, sockname = cfg.SAMPLERD_SOCK)
        if args.start_agg:
            print "start agg.."
            start_ldmsd(hosts = cfg.AGG_HOSTS, xprt = cfg.AGG_XPRT, port = cfg.AGG_PORT,
                              log = cfg.AGG_LOG, sockname = cfg.AGG_SOCK)
        if args.kill_samplerd:
            print "kill samplerd.."
            kill_ldmsd(hosts = cfg.SAMPLERD_HOSTS, xprt = cfg.SAMPLERD_XPRT,
                                port = cfg.SAMPLERD_PORT)
        if args.kill_agg:
            print "kill agg.."
            kill_ldmsd(hosts = cfg.AGG_HOSTS, xprt = cfg.AGG_XPRT, port = cfg.AGG_PORT)
        if args.kill_9_samplerd:
            print "kill 9 samplerd.."
            kill_9_ldmsd(hosts = cfg.SAMPLERD_HOSTS, xprt = cfg.SAMPLERD_XPRT,
                                port = cfg.SAMPLERD_PORT)
        if args.kill_9_agg:
            print "kill 9 agg.."
            kill_9_ldmsd(hosts = cfg.AGG_HOSTS, xprt = cfg.AGG_XPRT, port = cfg.AGG_PORT)
        if args.remove_samplerd_files:
            print "Removing the files of samplerd"
            remove_file(cfg.SAMPLERD_HOSTS, cfg.SAMPLERD_LOG)
            remove_file(cfg.SAMPLERD_HOSTS, cfg.SAMPLERD_SOCK)
        if args.remove_agg_files:
            print "Removing the files of aggregators"
            remove_file(cfg.AGG_HOSTS, cfg.AGG_LOG)
            remove_file(cfg.AGG_HOSTS, cfg.AGG_SOCK)
        if args.check_samplerd:
            print "Check samplerd ldmsd running? ...."
            print is_ldmsd_running(hosts = cfg.SAMPLERD_HOSTS,
                                              xprt = cfg.SAMPLERD_XPRT,
                                              port = cfg.SAMPLERD_PORT)
        if args.check_agg:
            print "Check agg ldmsd running? ...."
            print is_ldmsd_running(hosts = cfg.AGG_HOSTS, xprt = cfg.AGG_XPRT,
                                            port = cfg.AGG_PORT)
        if args.samplerd_pid:
            print "Getting samplerd pid"
            print get_ldmsd_pid(hosts = cfg.SAMPLERD_HOSTS, xprt = cfg.SAMPLERD_XPRT,
                                port = cfg.SAMPLERD_PORT)
        if args.agg_pid:
            print "Getting agg pid"
            print get_ldmsd_pid(hosts = cfg.AGG_HOSTS, xprt = cfg.AGG_XPRT,
                                port = cfg.AGG_PORT)
    except KeyboardInterrupt:
        return 0
    except Exception:
        traceback.print_exc()
        return 2

if __name__ == '__main__':
    sys.exit(main())

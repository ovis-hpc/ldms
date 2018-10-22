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
from ldmsd.ldmsd_util import remove_file
'''
Created on May 27, 2015

'''

# py.test version 2.5.1

import pytest
from time import sleep
from ldmsd_test.ldmsd_test_util import stop_test_ldmsds, start_test_ldmsds

@pytest.fixture(scope = "class")
def startup_ldmsd_processes(request, logger, cfg):
    logger.debug("------------ starting ldmsd processes")
    sleep(1)

    #=====Start of stop_ldmsd_processes()

    def stop_ldmsd_processes():
        logger.debug("------------ stopping ldmsd processes")

        stop_test_ldmsds(cfg.SAMPLERD_HOSTS, cfg.SAMPLERD_XPRT,
                         cfg.SAMPLERD_PORT, cfg.SAMPLERD_SOCK, logger)

        stop_test_ldmsds(cfg.AGG_HOSTS, cfg.AGG_XPRT, cfg.AGG_PORT,
                         cfg.SAMPLERD_SOCK, logger)

        logger.debug("------------ stopping ... DONE")

    #====== END of stop_ldmsd_processes()

    try:
        remove_file(hosts = cfg.SAMPLERD_HOSTS, filepath = cfg.SAMPLERD_LOG)
        remove_file(hosts = cfg.SAMPLERD_HOSTS, filepath = cfg.SAMPLERD_SOCK)
        remove_file(hosts = cfg.AGG_HOSTS, filepath = cfg.AGG_LOG)
        remove_file(hosts = cfg.AGG_HOSTS, filepath = cfg.AGG_SOCK)
        start_test_ldmsds(hosts = cfg.SAMPLERD_HOSTS,
                    xprt = cfg.SAMPLERD_XPRT,
                    port = cfg.SAMPLERD_PORT,
                    log = cfg.SAMPLERD_LOG,
                    sock = cfg.SAMPLERD_SOCK,
                    test_set_name = cfg.TEST_INSTANCE_PREFIX_NAME,
                    test_set_count = cfg.NUM_TEST_INSTANCES_PER_HOST,
                    test_metric_count = cfg.TEST_INSTANCE_NUM_METRICS,
                    inet_ctrl_port = cfg.SAMPLERD_INET_CTRL_PORT,
                    verbose = cfg.SAMPLERD_VERBOSE)

        start_test_ldmsds(hosts = cfg.AGG_HOSTS, xprt = cfg.AGG_XPRT,
                          port = cfg.AGG_PORT,
                          log = cfg.AGG_LOG, sock = cfg.AGG_SOCK,
                          inet_ctrl_port = cfg.AGG_INET_CTRL_PORT,
                          verbose = cfg.AGG_VERBOSE)

        logger.debug("------------ starting ... DONE")
    except:
        stop_ldmsd_processes()
        raise

    request.addfinalizer(stop_ldmsd_processes)

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
Created on Jul 6, 2015

'''

import pytest
from ovis_ldms import ldms
from ldmsd.ldmsd_config import ldmsdInetConfig
from ldmsd.ldmsd_setup import get_test_instance_name, kill_ldmsd,\
    is_ldmsd_running, kill_9_ldmsd, start_ldmsd
from time import sleep
from ldmsd.ldmsd_util import remove_file
from ldmsd_test.ldmsd_test_util import stop_test_ldmsds, start_test_ldmsds,\
    ldms_connect, microsec2sec
from sos import SOS

@pytest.fixture(scope = "class")
def start_2ndLevel_agg(request, logger, cfg):
    logger.debug("------------ starting the 2nd-level aggregators")
    def fin():
        logger.debug("------------ stopping the 2nd-level aggregators")
        stop_test_ldmsds(cfg.AGG2_HOSTS, cfg.AGG2_XPRT, cfg.AGG2_PORT,
                         cfg.AGG2_SOCK, logger)
        logger.debug("------------ stopping ... DONE")
    try:
        start_test_ldmsds(hosts = cfg.AGG2_HOSTS, xprt = cfg.AGG2_XPRT,
                           port = cfg.AGG2_PORT,
                           log = cfg.AGG2_LOG, sock = cfg.AGG2_SOCK,
                           inet_ctrl_port = cfg.AGG2_INET_CTRL_PORT)
        logger.debug("------------ starting ... DONE")
    except:
        fin()
        raise
    request.addfinalizer(fin)

@pytest.fixture(scope = "module")
def obj(request, logger, cfg):
    logger.debug("..initialize obj...")
    agg_2ndLevel_host = "localhost"
    agg_1stLevel_host = cfg.AGG_HOSTS[0]
    samplerd_host = cfg.SAMPLERD_HOSTS[0]
    instance = get_test_instance_name(samplerd_host, cfg.SAMPLERD_XPRT,
                                      cfg.SAMPLERD_PORT,
                                      cfg.TEST_INSTANCE_PREFIX_NAME, 1)

    logger.info("1st-first Aggregator -- host:xprt:port:inet_port" \
                " = {0}:{1}:{2}:{3}".format(agg_1stLevel_host, cfg.AGG_XPRT,
                                        cfg.AGG_PORT, cfg.AGG_INET_CTRL_PORT))
    logger.info("2nd-level Aggregator -- host:xprt:port:inet_port" \
                " = {0}:{1}:{2}:{3}".format(agg_1stLevel_host, cfg.AGG_XPRT,
                                        cfg.AGG_PORT, cfg.AGG_INET_CTRL_PORT))
    logger.info("Samplerd -- host:xprt:port:inet_port = {0}:{1}:{2}:{3}".format(
                                samplerd_host, cfg.SAMPLERD_XPRT,
                                cfg.SAMPLERD_PORT, cfg.SAMPLERD_INET_CTRL_PORT))
    logger.info("Instance name is {0}".format(instance))

    if cfg.SECRETWORD_FILE is None or cfg.SECRETWORD_FILE == "":
        secretword = None
        logger.info("No secret word")
    else:
        from ovis_ldms import ovis_auth
        secretword = ovis_auth.ovis_auth_get_secretword(cfg.SECRETWORD_FILE, None)
        logger.info("Secret word is '{0}'".format(secretword))

    return {'agg_2ndLevel_host': agg_2ndLevel_host,
            'agg_1stLevel_host': agg_1stLevel_host,
            'samplerd_host': samplerd_host,
            'instance': instance,
            'schema': "test_set",
            'store_pi': "store_sos",
            'container': "test_set",
            'secretword': secretword}

@pytest.fixture()
def agg_2ndLevel_conn(request, logger, cfg, obj):
    conn = ldms_connect(logger, obj['agg_2ndLevel_host'], cfg.AGG2_XPRT, cfg.AGG2_PORT)
    def fin():
        logger.debug("---disconnect {0}:{1}---".format(obj['agg_2ndLevel_host'],
                                                       cfg.AGG2_PORT))
        ldms.ldms_xprt_close(conn)
    request.addfinalizer(fin)
    return conn

@pytest.fixture()
def agg_1stLevel_conn(request, logger, cfg, obj):
    conn = ldms_connect(logger, obj['agg_1stLevel_host'], cfg.AGG_XPRT, cfg.AGG_PORT)
    def fin():
        logger.debug("---disconnect {0}:{1}---".format(obj['agg_1stLevel_host'],
                                                       cfg.AGG_PORT))
        ldms.ldms_xprt_close(conn)
    request.addfinalizer(fin)
    return conn

@pytest.fixture()
def samplerd_conn(request, logger, cfg, obj):
    conn = ldms_connect(logger, obj['samplerd_host'], cfg.SAMPLERD_XPRT,
                        cfg.SAMPLERD_PORT)
    def fin():
        logger.debug("---disconnect {0}:{1}---".format(obj['samplerd_host'],
                                                       cfg.SAMPLERD_PORT))
        ldms.ldms_xprt_close(conn)
    request.addfinalizer(fin)
    return conn


@pytest.mark.usefixtures("startup_ldmsd_processes", "start_2ndLevel_agg", "obj")
@pytest.mark.skipif("len(pytest.cfg.AGG2_HOSTS) == 0",
                    reason = "Not enough hosts are given for AGG2_HOSTS")
@pytest.mark.skipif("pytest.cfg.AGG2_HOSTS is None",
                    reason = "AGG2_HOSTS is None")
@pytest.mark.incremental
class Test_2level_aggregation:

    def test_samplerd_instance_existence(self, logger, cfg, obj, samplerd_conn):
        inst_dir = ldms.LDMS_xprt_dir(samplerd_conn)
        assert(inst_dir is not None)
        assert(len(inst_dir) == cfg.NUM_TEST_INSTANCES_PER_HOST)
        assert(obj['instance'] in inst_dir)

    def test_1stLevel_prdcr_add_start_cmd(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_1stLevel_host'],
                               cfg.AGG_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.prdcr_add(name = 'samplerd',
                                host = obj['samplerd_host'],
                                type = "active",
                                xprt = cfg.SAMPLERD_XPRT,
                                port = cfg.SAMPLERD_PORT,
                                interval = cfg.LDMSD_RECONNECT_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.prdcr_start(name = 'samplerd')
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_1stLevel_updtr_add_start_cmd(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_1stLevel_host'],
                               cfg.AGG_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.updtr_add(name = "all", interval = cfg.LDMSD_UPDATE_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_prdcr_add(name = "all", regex = ".*")
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_start(name = "all")
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_1stLevel_agg_instance_existed(self, logger, cfg, obj, agg_1stLevel_conn):
        passed_sec = 0
        timeout = 2 * microsec2sec(cfg.LDMSD_UPDATE_INTERVAL) # lookup + update. 2 times for safety
        inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
        assert(inst_dir is not None)
        while (len(inst_dir) == 0) and (passed_sec < timeout):
            passed_sec += 1
            sleep(1)
            inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
            assert(inst_dir is not None)

        logger.info("Wait time: {0}".format(passed_sec))
        assert obj['instance'] in inst_dir

    def test_2ndLevel_prdcr_add_start_cmd(self, logger, cfg, obj, agg_2ndLevel_conn):
        ctrl = ldmsdInetConfig(obj['agg_2ndLevel_host'],
                               cfg.AGG2_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.prdcr_add(name = '1st_agg',
                                host = obj['agg_1stLevel_host'],
                                type = "active",
                                xprt = cfg.AGG_XPRT,
                                port = cfg.AGG_PORT,
                                interval = cfg.LDMSD_RECONNECT_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.prdcr_start(name = '1st_agg')
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_2ndLevel_updtr_add_start_cmd(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_2ndLevel_host'],
                               cfg.AGG2_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.updtr_add(name = "all", interval = cfg.LDMSD_UPDATE_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_prdcr_add(name = "all", regex = ".*")
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_start(name = "all")
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_2ndLevel_instance_existed(self, logger, cfg, obj, agg_2ndLevel_conn):
        passed_sec = 0
        timeout = microsec2sec(cfg.LDMSD_UPDATE_INTERVAL)
        inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
        assert(inst_dir is not None)
        while (len(inst_dir) == 0) and (passed_sec < timeout):
            passed_sec += 1
            sleep(1)
            inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
            assert(inst_dir is not None)

        logger.info("Wait time: {0}".format(passed_sec))
        assert(obj['instance'] in inst_dir)

    def test_samplerd_die(self, logger, cfg, obj):
        kill_ldmsd(hosts = obj['samplerd_host'], xprt = cfg.SAMPLERD_XPRT,
                   port = cfg.SAMPLERD_PORT)
        is_running = is_ldmsd_running(hosts = obj['samplerd_host'],
                                      xprt = cfg.SAMPLERD_XPRT,
                                      port = cfg.SAMPLERD_PORT)
        if is_running[obj['samplerd_host']]:
            kill_9_ldmsd(hosts = obj['samplerd_host'],
                                      xprt = cfg.SAMPLERD_XPRT,
                                      port = cfg.SAMPLERD_PORT)
            remove_file(hosts = [obj['samplerd_host']], filepath = cfg.SAMPLERD_SOCK)

        # Sleep to make sure that the aggregators do not crash.
        # If the test doesn't sleep, the aggregators could die after
        # the check point.
        sleep(3)

        is_agg_running = is_ldmsd_running(hosts = [obj['agg_1stLevel_host']],
                                      xprt = cfg.AGG_XPRT,
                                      port = cfg.AGG_PORT)
        assert(is_agg_running[obj['agg_1stLevel_host']])
        is_agg_running = is_ldmsd_running(hosts = [obj['agg_2ndLevel_host']],
                                      xprt = cfg.AGG2_XPRT,
                                      port = cfg.AGG2_PORT)
        assert(is_agg_running[obj['agg_2ndLevel_host']])

    def test_1stLevel_instance_after_samplerd_died(self, logger, cfg, obj, agg_1stLevel_conn):
        # The aggregator should know right away that the set is gone
        # because the set is reset when ldmsd receives a DISCONNECTED event.
        sleep(1)
        inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
        assert(inst_dir is not None)
        assert(obj['instance'] not in inst_dir)

    def test_2ndLevel_instance_after_samplerd_died(self, logger, cfg, obj, agg_2ndLevel_conn):
        # Since dir_update is activated, as soon as the local set on the 1st aggregator
        # is deleted, the 2nd-level aggregator should know right away.
        sleep(2)
        inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
        assert(inst_dir is not None)
        assert(obj['instance'] not in inst_dir)

    def test_samplerd_comeback(self, logger, cfg, obj):
        start_ldmsd(hosts = obj['samplerd_host'],
                    xprt = cfg.SAMPLERD_XPRT,
                    port = cfg.SAMPLERD_PORT,
                    log = cfg.SAMPLERD_LOG,
                    sock = cfg.SAMPLERD_SOCK,
                    test_set_name = cfg.TEST_INSTANCE_PREFIX_NAME,
                    test_set_count = cfg.NUM_TEST_INSTANCES_PER_HOST,
                    test_metric_count = cfg.TEST_INSTANCE_NUM_METRICS,
                    inet_ctrl_port = cfg.SAMPLERD_INET_CTRL_PORT)
        is_samplerd_started = is_ldmsd_running(hosts = obj['samplerd_host'],
                                               xprt = cfg.SAMPLERD_XPRT,
                                               port = cfg.SAMPLERD_PORT)
        assert(is_samplerd_started[obj['samplerd_host']])

        sleep(3)

        is_running = is_ldmsd_running(hosts = obj['agg_1stLevel_host'],
                                      xprt = cfg.AGG_XPRT,
                                      port = cfg.AGG_PORT)
        assert(is_running[obj['agg_1stLevel_host']])

    def test_1stLevel_instance_after_samplerd_revived(self, logger, cfg, obj, agg_1stLevel_conn):
        passed_sec = 0
        # wait time must be at least the reconnect interval.
        # + 2 is time buffer to do dir and lookup
        timeout = microsec2sec(cfg.LDMSD_RECONNECT_INTERVAL) + 1
        inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
        assert(inst_dir is not None)
        while (len(inst_dir) == 0) and (passed_sec < timeout):
            passed_sec += 1
            sleep(1)
            inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
            assert(inst_dir is not None)

        logger.info("Wait time: {0}".format(passed_sec))
        assert(obj['instance'] in inst_dir)

    def test_2ndLevel_instance_after_samplerd_revived(self, logger, cfg, obj, agg_2ndLevel_conn):
        passed_sec = 0
        # As soon as the local set on the 1st-level aggregator is created,
        # a dir_update is sent to the 2nd-level aggregator for the new added set.
        # the timeout period has nothing to do with the reconnect/update intervals.
        timeout = 2
        inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
        assert(inst_dir is not None)
        while (len(inst_dir) == 0) and (passed_sec < timeout):
            passed_sec += 1
            sleep(1)
            inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
            assert(inst_dir is not None)

        logger.info("Wait time: {0}".format(passed_sec))
        assert(obj['instance'] in inst_dir)

    def test_1stLevel_agg_die(self, logger, cfg, obj):
        kill_ldmsd(hosts = [obj['agg_1stLevel_host']], xprt = cfg.AGG_XPRT,
                   port = cfg.AGG_PORT)

        is_running = is_ldmsd_running(
                                    hosts = [obj['agg_1stLevel_host']],
                                    xprt = cfg.AGG_XPRT,
                                    port = cfg.AGG_PORT)
        if is_running[obj['agg_1stLevel_host']]:
            kill_9_ldmsd(hosts = [obj['agg_1stLevel_host']], xprt = cfg.AGG_XPRT,
                   port = cfg.AGG_PORT)
            remove_file(hosts = [obj['agg_1stLevel_host']], filepath = cfg.AGG_SOCK)

        is_running = is_ldmsd_running(
                                    hosts = [obj['agg_1stLevel_host']],
                                    xprt = cfg.AGG_XPRT,
                                    port = cfg.AGG_PORT)
        assert(not is_running[obj['agg_1stLevel_host']])

        sleep(3)

        is_samplerd_running = is_ldmsd_running(hosts = [obj['samplerd_host']],
                                               xprt = cfg.SAMPLERD_XPRT,
                                               port = cfg.SAMPLERD_PORT)
        assert(is_samplerd_running[obj['samplerd_host']])

        is_2ndLevel_agg_running = is_ldmsd_running(hosts = [obj['agg_2ndLevel_host']],
                                                   xprt = cfg.AGG2_XPRT,
                                                   port = cfg.AGG2_PORT)
        assert(is_2ndLevel_agg_running[obj['agg_2ndLevel_host']])

    def test_samplerd_after_1st_agg_died(self, logger, cfg, obj, samplerd_conn):
        inst_dir = ldms.LDMS_xprt_dir(samplerd_conn)
        assert(inst_dir is not None)
        assert(obj['instance'] in inst_dir)

    def test_2ndLevel_isntance_after_1st_agg_died(self, logger, cfg, obj, agg_2ndLevel_conn):
        sleep(1)
        inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
        assert(inst_dir is not None)
        assert(obj['instance'] not in inst_dir)

    def test_1stLevel_agg_comeback(self, logger, cfg, obj):
        start_ldmsd(hosts = [obj['agg_1stLevel_host']], xprt = cfg.AGG_XPRT,
                    port = cfg.AGG_PORT, log = cfg.AGG_LOG,
                    sock = cfg.AGG_SOCK,
                    inet_ctrl_port = cfg.AGG_INET_CTRL_PORT)
        is_1st_agg_started = is_ldmsd_running(hosts = obj['agg_1stLevel_host'],
                                               xprt = cfg.AGG_XPRT,
                                               port = cfg.AGG_PORT)
        assert(is_1st_agg_started[obj['agg_1stLevel_host']])

    def test_1stLevel_prdcr_add_start_cmd_after_revived(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_1stLevel_host'],
                               cfg.AGG_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.prdcr_add(name = 'samplerd',
                                host = obj['samplerd_host'],
                                type = "active",
                                xprt = cfg.SAMPLERD_XPRT,
                                port = cfg.SAMPLERD_PORT,
                                interval = cfg.LDMSD_RECONNECT_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.prdcr_start(name = 'samplerd')
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_1stLevel_updtr_add_start_cmd_after_revived(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_1stLevel_host'],
                               cfg.AGG_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.updtr_add(name = "all", interval = cfg.LDMSD_UPDATE_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_prdcr_add(name = "all", regex = ".*")
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_start(name = "all")
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_1stLevel_isntance_after_revived(self, logger, cfg, obj, agg_1stLevel_conn):
        passed_sec = 0
        timeout = microsec2sec(cfg.LDMSD_UPDATE_INTERVAL) # lookup + update. 2 times for safety
        inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
        assert(inst_dir is not None)
        while (len(inst_dir) == 0) and (passed_sec < timeout):
            passed_sec += 1
            sleep(1)
            inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
            assert(inst_dir is not None)

        logger.info("Wait time: {0}".format(passed_sec))
        assert obj['instance'] in inst_dir

    def test_2ndLevel_instance_after_1stLevel_revived(self, logger, cfg, obj, agg_2ndLevel_conn):
        passed_sec = 0
        timeout = microsec2sec(cfg.LDMSD_RECONNECT_INTERVAL) + 1
        inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
        assert(inst_dir is not None)
        while (len(inst_dir) == 0) and (passed_sec < timeout):
            passed_sec += 1
            sleep(1)
            inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
            assert(inst_dir is not None)

        logger.info("Wait time: {0}".format(passed_sec))
        assert(obj['instance'] in inst_dir)


@pytest.mark.usefixtures("startup_ldmsd_processes", "start_2ndLevel_agg", "obj")
@pytest.mark.skipif("len(pytest.cfg.AGG2_HOSTS) == 0",
                    reason = "Not enough hosts are given for AGG2_HOSTS")
@pytest.mark.skipif("pytest.cfg.AGG2_HOSTS is None",
                    reason = "AGG2_HOSTS is None")
@pytest.mark.incremental
class Test_2level_aggregation_with_store:

    def test_samplerd_instance_existence(self, logger, cfg, obj, samplerd_conn):
        logger.debug("--- before ldms_xprt_dir")
        inst_dir = ldms.LDMS_xprt_dir(samplerd_conn)
        logger.debug("--- after ldms_xprt_dir")
        assert(inst_dir is not None)
        assert(len(inst_dir) == cfg.NUM_TEST_INSTANCES_PER_HOST)
        assert(obj['instance'] in inst_dir)

    def test_1stLevel_prdcr_add_start_cmd(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_1stLevel_host'],
                               cfg.AGG_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.prdcr_add(name = 'samplerd',
                                host = obj['samplerd_host'],
                                type = "active",
                                xprt = cfg.SAMPLERD_XPRT,
                                port = cfg.SAMPLERD_PORT,
                                interval = cfg.LDMSD_RECONNECT_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.prdcr_start(name = 'samplerd')
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_1stLevel_updtr_add_start_cmd(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_1stLevel_host'],
                               cfg.AGG_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.updtr_add(name = "all", interval = cfg.LDMSD_UPDATE_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_prdcr_add(name = "all", regex = ".*")
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_start(name = "all")
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_1stLevel_agg_instance_existed(self, logger, cfg, obj, agg_1stLevel_conn):
        passed_sec = 0
        timeout = 2 * microsec2sec(cfg.LDMSD_UPDATE_INTERVAL) # lookup + update. 2 times for safety
        inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
        assert(inst_dir is not None)
        while (len(inst_dir) == 0) and (passed_sec < timeout):
            passed_sec += 1
            sleep(1)
            inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
            assert(inst_dir is not None)

        logger.info("Wait time: {0}".format(passed_sec))
        assert obj['instance'] in inst_dir

    def test_2ndLevel_prdcr_updtr_cmd(self, logger, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_2ndLevel_host'],
                               cfg.AGG2_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.prdcr_add(name = '1st_agg',
                                host = obj['agg_1stLevel_host'],
                                type = "active",
                                xprt = cfg.AGG_XPRT,
                                port = cfg.AGG_PORT,
                                interval = cfg.LDMSD_RECONNECT_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.prdcr_start(name = '1st_agg')
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_add(name = "all", interval = cfg.LDMSD_UPDATE_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_prdcr_add(name = "all", regex = ".*")
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_start(name = "all")
        result = result.rstrip()
        assert(result == "0")
        ctrl.close()

    def test_2ndLevel_store_cmd(self, logger, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_2ndLevel_host'],
                               cfg.AGG2_INET_CTRL_PORT, obj['secretword'])

        logger.info("store: {0}".format(obj['store_pi']))

        result = ctrl.load(name = obj['store_pi'])
        assert(result == "0")

        logger.debug("config ....")
        result = ctrl.config(name = obj['store_pi'], path = cfg.STORE_PATH)
        assert(result == "0")

        logger.debug("strgp_add ....")
        result = ctrl.strgp_add(name = "all", plugin = obj['store_pi'],
                                container = obj['container'],
                                schema = obj['schema'])
        result = result.rstrip();
        assert(result == "0")

        logger.debug("strgp_prdcr_add ....")
        result = ctrl.strgp_prdcr_add(name = "all", regex = ".*")
        result = result.rstrip()
        assert(result == "0")

        logger.debug("strgp_start ....")
        result = ctrl.strgp_start(name = "all")
        result = result.rstrip()
        assert(result == "0")
        ctrl.close()

    def test_2ndLevel_running_after_store(self, cfg, obj, logger):
        nap = 5
        sleep(nap)
        t = nap
        while (t <= 60):
            is_running = is_ldmsd_running(hosts = obj['agg_2ndLevel_host'],
                                          xprt = cfg.AGG2_XPRT,
                                          port = cfg.AGG2_PORT)
            assert(is_running[obj['agg_2ndLevel_host']])
            logger.info("sleeping for {0}".format(nap))
            sleep(nap)
            t += nap

    def test_2ndLevel_store(self, logger, cfg, obj):
        sosc = SOS.Container("{0}/{1}".format(cfg.STORE_PATH, obj['container']))
        for schema_name, schema in sosc.schemas.iteritems():
            if schema_name == obj['schema']:
                break
            schema = None

        # Not a test point
        assert(schema is not None,
               "The testing sos schema does not exist. Check the test code")

        tstp_attr = schema.attr("Timestamp")
        tstp_iter = tstp_attr.iterator()

        last_record_time = tstp_iter.end().values['Timestamp'].__int__()
        logger.debug("last timestamp: {0}".format(last_record_time))
        assert(last_record_time is not None)
        assert(last_record_time != "")

        prev_last_record_time = last_record_time

        logger.info("Sleeping for 20 seconds.")
        sleep(20 * microsec2sec(cfg.LDMSD_UPDATE_INTERVAL))
        last_record_time = tstp_iter.end().values['Timestamp'].__int__()
        logger.debug("last timestamp: {0}".format(last_record_time))
        assert(last_record_time > prev_last_record_time)

    def test_samplerd_die(self, logger, cfg, obj):
        kill_ldmsd(hosts = obj['samplerd_host'], xprt = cfg.SAMPLERD_XPRT,
                   port = cfg.SAMPLERD_PORT)
        is_running = is_ldmsd_running(hosts = obj['samplerd_host'],
                                      xprt = cfg.SAMPLERD_XPRT,
                                      port = cfg.SAMPLERD_PORT)
        if is_running[obj['samplerd_host']]:
            kill_9_ldmsd(hosts = obj['samplerd_host'],
                                      xprt = cfg.SAMPLERD_XPRT,
                                      port = cfg.SAMPLERD_PORT)
            remove_file(hosts = [obj['samplerd_host']], filepath = cfg.SAMPLERD_SOCK)

        # Sleep to make sure that the aggregators do not crash.
        # If the test doesn't sleep, the aggregators could die after
        # the check point.
        sleep(3)

        is_agg_running = is_ldmsd_running(hosts = [obj['agg_1stLevel_host']],
                                      xprt = cfg.AGG_XPRT,
                                      port = cfg.AGG_PORT)
        assert(is_agg_running[obj['agg_1stLevel_host']])
        is_agg_running = is_ldmsd_running(hosts = [obj['agg_2ndLevel_host']],
                                      xprt = cfg.AGG2_XPRT,
                                      port = cfg.AGG2_PORT)
        assert(is_agg_running[obj['agg_2ndLevel_host']])

    def test_1stLevel_instance_after_samplerd_died(self, logger, cfg, obj, agg_1stLevel_conn):
        # The aggregator should know right away that the set is gone
        # because the set is reset when ldmsd receives a DISCONNECTED event.
        sleep(1)
        inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
        assert(inst_dir is not None)
        assert(obj['instance'] not in inst_dir)

    def test_2ndLevel_instance_after_samplerd_died(self, logger, cfg, obj, agg_2ndLevel_conn):
        # Since dir_update is activated, as soon as the local set on the 1st aggregator
        # is deleted, the 2nd-level aggregator should know right away.
        sleep(2)
        inst_dir = ldms.LDMS_xprt_dir(agg_2ndLevel_conn)
        assert(inst_dir is not None)
        assert(obj['instance'] not in inst_dir)

    def test_samplerd_comeback(self, logger, cfg, obj):
        start_ldmsd(hosts = obj['samplerd_host'],
                    xprt = cfg.SAMPLERD_XPRT,
                    port = cfg.SAMPLERD_PORT,
                    log = cfg.SAMPLERD_LOG,
                    sock = cfg.SAMPLERD_SOCK,
                    test_set_name = cfg.TEST_INSTANCE_PREFIX_NAME,
                    test_set_count = cfg.NUM_TEST_INSTANCES_PER_HOST,
                    test_metric_count = cfg.TEST_INSTANCE_NUM_METRICS,
                    inet_ctrl_port = cfg.SAMPLERD_INET_CTRL_PORT)
        is_samplerd_started = is_ldmsd_running(hosts = obj['samplerd_host'],
                                               xprt = cfg.SAMPLERD_XPRT,
                                               port = cfg.SAMPLERD_PORT)
        assert(is_samplerd_started[obj['samplerd_host']])

        sleep(3)

        is_running = is_ldmsd_running(hosts = obj['agg_1stLevel_host'],
                                      xprt = cfg.AGG_XPRT,
                                      port = cfg.AGG_PORT)
        assert(is_running[obj['agg_1stLevel_host']])

    def test_2ndLevel_store_after_samplerd_revived(self, logger, cfg, obj):
        sleep(4 * microsec2sec(cfg.LDMSD_UPDATE_INTERVAL))
        sosc = SOS.Container("{0}/{1}".format(cfg.STORE_PATH, obj['container']))
        for schema_name, schema in sosc.schemas.iteritems():
            if schema_name == obj['schema']:
                break
            schema = None

        # Not a test point
        assert(schema is not None,
               "The testing sos schema does not exist. Check the test code")

        tstp_attr = schema.attr("Timestamp")
        tstp_iter = tstp_attr.iterator()
        last_record_time = tstp_iter.end().values['Timestamp'].__int__()
        logger.debug("last timestamp: {0}".format(last_record_time))
        assert(last_record_time is not None)
        assert(last_record_time != "")

        prev_last_record_time = last_record_time
        sleep(20 * microsec2sec(cfg.LDMSD_UPDATE_INTERVAL))
        last_record_time = tstp_iter.end().values['Timestamp'].__int__()
        logger.debug("last timestamp: {0}".format(last_record_time))
        assert(last_record_time > prev_last_record_time)

    def test_1stLevel_agg_die(self, logger, cfg, obj):
        kill_ldmsd(hosts = [obj['agg_1stLevel_host']], xprt = cfg.AGG_XPRT,
                   port = cfg.AGG_PORT)

        is_running = is_ldmsd_running(
                                    hosts = [obj['agg_1stLevel_host']],
                                    xprt = cfg.AGG_XPRT,
                                    port = cfg.AGG_PORT)
        if is_running[obj['agg_1stLevel_host']]:
            kill_9_ldmsd(hosts = [obj['agg_1stLevel_host']], xprt = cfg.AGG_XPRT,
                   port = cfg.AGG_PORT)
            remove_file(hosts = [obj['agg_1stLevel_host']], filepath = cfg.AGG_SOCK)

        is_running = is_ldmsd_running(
                                    hosts = [obj['agg_1stLevel_host']],
                                    xprt = cfg.AGG_XPRT,
                                    port = cfg.AGG_PORT)
        assert(not is_running[obj['agg_1stLevel_host']])

        sleep(3)

        is_samplerd_running = is_ldmsd_running(hosts = [obj['samplerd_host']],
                                               xprt = cfg.SAMPLERD_XPRT,
                                               port = cfg.SAMPLERD_PORT)
        assert(is_samplerd_running[obj['samplerd_host']])

        is_2ndLevel_agg_running = is_ldmsd_running(hosts = [obj['agg_2ndLevel_host']],
                                                   xprt = cfg.AGG2_XPRT,
                                                   port = cfg.AGG2_PORT)
        assert(is_2ndLevel_agg_running[obj['agg_2ndLevel_host']])

    def test_1stLevel_agg_comeback(self, logger, cfg, obj):
        start_ldmsd(hosts = [obj['agg_1stLevel_host']], xprt = cfg.AGG_XPRT,
                    port = cfg.AGG_PORT, log = cfg.AGG_LOG,
                    sock = cfg.AGG_SOCK,
                    inet_ctrl_port = cfg.AGG_INET_CTRL_PORT)
        is_1st_agg_started = is_ldmsd_running(hosts = obj['agg_1stLevel_host'],
                                               xprt = cfg.AGG_XPRT,
                                               port = cfg.AGG_PORT)
        assert(is_1st_agg_started[obj['agg_1stLevel_host']])

    def test_1stLevel_prdcr_add_start_cmd_after_revived(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_1stLevel_host'],
                               cfg.AGG_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.prdcr_add(name = 'samplerd',
                                host = obj['samplerd_host'],
                                type = "active",
                                xprt = cfg.SAMPLERD_XPRT,
                                port = cfg.SAMPLERD_PORT,
                                interval = cfg.LDMSD_RECONNECT_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.prdcr_start(name = 'samplerd')
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_1stLevel_updtr_add_start_cmd_after_revived(self, cfg, obj):
        ctrl = ldmsdInetConfig(obj['agg_1stLevel_host'],
                               cfg.AGG_INET_CTRL_PORT, obj['secretword'])
        result = ctrl.updtr_add(name = "all", interval = cfg.LDMSD_UPDATE_INTERVAL)
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_prdcr_add(name = "all", regex = ".*")
        result = result.rstrip()
        assert(result == "0")

        result = ctrl.updtr_start(name = "all")
        result = result.rstrip()
        assert(result == "0")

        ctrl.close()

    def test_1stLevel_isntance_after_revived(self, logger, cfg, obj, agg_1stLevel_conn):
        passed_sec = 0
        timeout = microsec2sec(cfg.LDMSD_UPDATE_INTERVAL) # lookup + update. 2 times for safety
        inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
        assert(inst_dir is not None)
        while (len(inst_dir) == 0) and (passed_sec < timeout):
            passed_sec += 1
            sleep(1)
            inst_dir = ldms.LDMS_xprt_dir(agg_1stLevel_conn)
            assert(inst_dir is not None)

        logger.info("Wait time: {0}".format(passed_sec))
        assert obj['instance'] in inst_dir

    def test_2ndLevel_store_after_1stLevel_revived(self, logger, cfg, obj):
        sleep(4 * microsec2sec(cfg.LDMSD_UPDATE_INTERVAL))
        sosc = SOS.Container("{0}/{1}".format(cfg.STORE_PATH, obj['container']))
        for schema_name, schema in sosc.schemas.iteritems():
            if schema_name == obj['schema']:
                break
            schema = None

        # Not a test point
        assert(schema is not None,
               "The testing sos schema does not exist. Check the test code")

        tstp_attr = schema.attr("Timestamp")
        tstp_iter = tstp_attr.iterator()

        last_record_time = tstp_iter.end().values["Timestamp"].__int__()
        logger.debug("last timestamp: {0}".format(last_record_time))
        assert(last_record_time is not None)
        assert(last_record_time != "")

        prev_last_record_time = last_record_time
        sleep(20 * microsec2sec(cfg.LDMSD_UPDATE_INTERVAL))
        last_record_time = tstp_iter.end().values["Timestamp"].__int__()
        logger.debug("last timestamp: {0}".format(last_record_time))
        assert(last_record_time > prev_last_record_time)

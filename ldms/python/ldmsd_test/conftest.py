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
from ldmsd_test.ldmsd_test_util import load_test_config
'''
Created on May 27, 2015

'''

# py.test version 2.5.1

import pytest
import logging
from ldmsd.ldmsd_setup import start_ldmsd, is_ldmsd_running, kill_ldmsd,\
    kill_9_ldmsd
from ovis_ldms.ldms import ldms_init
from ldmsd.ldmsd_util import remove_file, get_var_from_file

def __second_2_microsecond(sec):
    return sec * 1000000

def pytest_addoption(parser):
    parser.addoption("--pre-mem", action="store", default=1024,
                     help = "Pre-allocated memory size by ldms")
    levels_list = [logging.DEBUG, logging.INFO, logging.ERROR]
    parser.addoption("--verbosity", action = "store",
                     choices = [logging._levelNames[level] for level in levels_list],
                     default = logging._levelNames[logging.INFO],
                     help = "Verbosity of logging")
    parser.addoption("--config-file", action = "store",
                     help = "Path to the ldmsd test config file.")

def pytest_configure(config):
    path = config.getoption("--config-file")
    cfg = load_test_config(path)
    for host_list in [cfg.AGG_HOSTS, cfg.AGG2_HOSTS, cfg.STORED_HOSTS]:
        if "localhost" not in host_list:
            host_list += ["localhost"]

    pytest.cfg = cfg

def pytest_runtest_makereport(item, call):
    if "incremental" in item.keywords:
        if call.excinfo is not None:
            parent = item.parent
            parent._previousfailed = item

def pytest_runtest_setup(item):
    if "incremental" in item.keywords:
        previousfailed = getattr(item.parent, "_previousfailed", None)
        if previousfailed is not None:
            pytest.xfail("previous test failed {0}".format(previousfailed.name))

###########################################
# logging
###########################################
logging.basicConfig(level = logging.INFO,
                    format = '\n%(levelname)s: %(message)s')

@pytest.fixture(scope = "session", autouse = True)
def logger(request):
    level_s = request.config.getoption("--verbosity")
    level = logging._levelNames[level_s]
    logger = logging.getLogger()
    logger.setLevel(level)
    return logger

#############################################
# Get the config variables.
#############################################

@pytest.fixture(scope = "session", autouse = True)
def cfg(request, logger):
    return pytest.cfg

###########################################
# Session, modules and test cases divider
###########################################
@pytest.fixture(scope = "session", autouse = True)
def divider_session(request):
    logging.info("############### session start ###############")
    def fin():
        logging.info("############### session done ###############")
    request.addfinalizer(fin)

@pytest.fixture(scope = "module", autouse = True)
def divider_module(request):
    logging.info("########## module '{0}' start ##########".format(
                                                request.module.__name__))
    def fin():
        logging.info("########## module '{0}' done ##########".format(
                                                    request.module.__name__))
    request.addfinalizer(fin)

@pytest.fixture(scope = "class", autouse = True)
def divider_class(request):
    logging.info("############### class '{0}' start ################".format(
                                                request.cls.__name__))
    def fin():
        logging.info("############### class '{0}' done ################".format(
                                                    request.cls.__name__))
    request.addfinalizer(fin)

@pytest.fixture(scope = "function", autouse = True)
def divider_function(request):
    logging.info("##### {0} start#####".format(
                                                request.function.__name__))
    def fin():
        logging.info("##### {0} done#####".format(
                                                    request.function.__name__))
    request.addfinalizer(fin)

###########################################
# Things that really matter
###########################################
@pytest.fixture(scope = "session", autouse = True)
def ldmsd_test_init(request):
    size = request.config.getoption("--pre-mem")
    ldms_init(size)

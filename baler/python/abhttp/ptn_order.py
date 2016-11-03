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

import logging
from datatype import *
logger = logging.getLogger(__name__)

# a collection of comparator
COMP = {}

def register_ptn_cmp(name, _cmp):
    """Register a comparator to the comparator collection.

    Registering an existing `name` will overwrite the previously registered
    comparator.

    `_cmp` is a function (x,y) returning:
        -1 if x is before y,
        1  if x is after y, and
        0  if x and y are of the same order.
    """
    COMP[name] = _cmp


def get_ptn_cmp(name):
    return COMP[name]



#################################
####### basic comparators #######
#################################

def __ptn_attr_asc(attr, x, y):
    _x = x.__getattribute__(attr)
    _y = y.__getattribute__(attr)
    if _x < _y:
        return -1
    if _x > _y:
        return 1
    return 0


def __ptn_cmp_count_asc(x,y):
    return __ptn_attr_asc("count", x, y)
register_ptn_cmp("count_asc", __ptn_cmp_count_asc)

def __ptn_cmp_count_desc(x,y):
    return __ptn_attr_asc("count", y, x)
register_ptn_cmp("count_desc", __ptn_cmp_count_desc)


def __ptn_cmp_ptn_id_asc(x,y):
    return __ptn_attr_asc("ptn_id", x, y)
register_ptn_cmp("ptn_id_asc", __ptn_cmp_ptn_id_asc)

def __ptn_cmp_ptn_id_desc(x,y):
    return __ptn_attr_asc("ptn_id", y, x)
register_ptn_cmp("ptn_id_desc", __ptn_cmp_ptn_id_desc)


def __ptn_cmp_first_seen_asc(x,y):
    return __ptn_attr_asc("first_seen", x, y)
register_ptn_cmp("first_seen_asc", __ptn_cmp_first_seen_asc)

def __ptn_cmp_first_seen_desc(x,y):
    return __ptn_attr_asc("first_seen", y, x)
register_ptn_cmp("first_seen_desc", __ptn_cmp_first_seen_desc)


def __ptn_cmp_last_seen_asc(x,y):
    return __ptn_attr_asc("last_seen", x, y)
register_ptn_cmp("last_seen_asc", __ptn_cmp_last_seen_asc)

def __ptn_cmp_last_seen_desc(x,y):
    return __ptn_attr_asc("last_seen", y, x)
register_ptn_cmp("last_seen_desc", __ptn_cmp_last_seen_desc)


def __ptn_eng_ratio(p):
    try:
        _len = len(p.tokens)
    except Exception:
        logger.warn(p)
        logger.warn(p.tokens)
        raise
    _count = 0
    for t in p.tokens:
        if t.tok_type == TokenType.ENG:
            _count += 1
    return float(_count)/_len

def __ptn_cmp_eng_asc(x,y):
    _x = __ptn_eng_ratio(x)
    _y = __ptn_eng_ratio(y)
    if _x < _y:
        return -1
    if _x > _y:
        return 1
    return 0
register_ptn_cmp("eng_asc", __ptn_cmp_eng_asc)

def __ptn_cmp_eng_desc(x,y):
    return __ptn_cmp_eng_asc(y,x)
register_ptn_cmp("eng_desc", __ptn_cmp_eng_desc)

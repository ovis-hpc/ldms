#!/usr/bin/env python
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

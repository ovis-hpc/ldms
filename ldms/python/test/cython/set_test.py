#!/usr/bin/python3

# This is a module to help constructing LDMS schema and set and verify the data

import re
from ovis_ldms import ldms

SCHEMA_NAME = "simple"

# Metric definition can be in the following form:
# - list of ( NAME, TYPE, [LEN, [UNITS, [IS_META]]] )
#   NAME and TYPE are always required. LEN is required only if the type is an
#   array type, otherwise LEN is optional (and actually ignored).  UNITS and
#   IS_META are optional. Due to `*args` Python order, LEN needs to be specified
#   if we want to specify UNITS.
# - a dictionary with "name", "type", "count", "units", and "meta" as keys.
#   "name" and "type" are always required. "count" is only required if the type
#   is an array type, otherwise it can be omitted. "units" and "meta" can be
#   omitted.
#
# The TYPEs could be `str` equivalent of the type name (e.g. "char", "int8",
# "float", and "f32") or `V_*` enumeration. `str` maps to "char[]" and
# `int` maps to "int64".

SCHEMA = ldms.Schema(name = SCHEMA_NAME, metric_list = [
        ( "a_char"    , "char"   ), # no units specified
        ( "an_int8"   , "int8"   , 1, "B"), # needs to specify LEN before UNITS

        # using dict "length" is safely ignored in this case.
        { "name": "an_uint8", "type": "uint8", "units": "G" },

        ( "an_int16"  , "int16"  ),
        ( "an_uint16" , "uint16" ),
        ( "an_int32"  , "int32"  ),
        ( "an_uint32" , "uint32" ),
        ( "an_int64"  , int      ), # `int` is equivalent to "int64"
        ( "an_uint64" , "uint64" ),
        ( "a_float"   , "float"  ),
        ( "a_double"  , "double" ),
        ( "a_str"           , str                   , 5 ), # "char[]"
        ( "an_int8_array"   , "int8[]"              , 5 ),
        ( "an_uint8_array"  , "uint8_array"         , 5 ), # "_array" works too
        ( "an_int16_array"  , ldms.V_S16_ARRAY , 5 ), # enum
        ( "an_uint16_array" , "uint16[]"            , 5 ),
        ( "an_int32_array"  , "int32[]"             , 5 ),
        ( "an_uint32_array" , "uint32[]"            , 5 ),
        ( "an_int64_array"  , "int64[]"             , 5 ),
        ( "an_uint64_array" , "uint64[]"            , 5 ),
        ( "a_float_array"   , "float[]"             , 5 , "SEC" ),
        ( "a_double_array"  , "double[]"            , 5 , "uSEC" ),
    ])

SET_NAME = "the_set"

# List of UNITS for comparison
UNITS = [ "" ] * 22
UNITS[1] = "B"
UNITS[2] = "G"
UNITS[-2] = "SEC"
UNITS[-1] = "uSEC"

def populate_data(lset):
    if type(lset) != ldms.Set:
        raise TypeError(".set must be an ldms.Set")
    lset.transaction_begin()
    lset[0] = 'a' # single metric value assignment
    lset[1:11] = range(1, 11) # metric value assignment by `slice`
    lset["a_str"] = "abcd" # assign by metric name
    for _id in range(12, 22): # array types
        lset[_id] = range(_id, _id+5) # assign the whole array
    lset[-1][2:4] = [ 55, 66 ] # sub-array assignment using slice.
                               # NOTE: `-1` refers to the last member.
                               #       In this case, "a_double_array" metric
    lset[-2][1] = 77 # metric array element assignment
                     # NOTE: `-2` refers to the 2nd to the last member.
                     #       In this case, "a_float_array" metric
    lset[-2][-1] = 88 # The negative index works on the metric array element too
    lset.transaction_end()

EXPECTED_DATA = {
        "a_char": "a",
        "an_int8": 1,
        "an_uint8": 2,
        "an_int16": 3,
        "an_uint16": 4,
        "an_int32": 5,
        "an_uint32": 6,
        "an_int64": 7,
        "an_uint64": 8,
        "a_float": 9.0,
        "a_double": 10.0,
        "a_str": "abcd",
        "an_int8_array": [ 12, 13, 14, 15, 16 ],
        "an_uint8_array": [ 13, 14, 15, 16, 17 ],
        "an_int16_array": [ 14, 15, 16, 17, 18 ],
        "an_uint16_array": [ 15, 16, 17, 18, 19 ],
        "an_int32_array": [ 16, 17, 18, 19, 20 ],
        "an_uint32_array": [ 17, 18, 19, 20, 21 ],
        "an_int64_array": [ 18, 19, 20, 21, 22 ],
        "an_uint64_array": [ 19, 20, 21, 22, 23 ],
        "a_float_array": [ 20.0, 77.0, 22.0, 23.0, 88.0 ],
        "a_double_array": [ 21.0, 22.0, 55.0, 66.0, 25.0 ]
    }


#######################
#   ldms_ls parsing   #
#######################
_META_BEGIN = r'(?P<meta_begin>Schema\s+Instance\s+Flags.*\s+Info)'
_META_DASHES = r'(?:^(?P<meta_dashes>[ -]+)$)'
_META_SUMMARY = '(?:'+ \
                r'Total Sets: (?P<meta_total_sets>\d+), ' + \
                r'Meta Data \(kB\):? (?P<meta_sz>\d+(?:\.\d+)?), ' + \
                r'Data \(kB\):? (?P<data_sz>\d+(?:\.\d+)?), ' + \
                r'Memory \(kB\):? (?P<mem_sz>\d+(?:\.\d+)?)' + \
                ')'
_META_DATA = r'(?:' + \
             r'(?P<meta_schema>\S+)\s+' + \
             r'(?P<meta_inst>\S+)\s+' + \
             r'(?P<meta_flags>\S+)\s+' + \
             r'(?P<meta_msize>\d+)\s+' + \
             r'(?P<meta_dsize>\d+)\s+' + \
             r'(?P<meta_uid>\d+)\s+' + \
             r'(?P<meta_gid>\d+)\s+' + \
             r'(?P<meta_perm>-(?:[r-][w-][x-]){3})\s+' + \
             r'(?P<meta_update>\d+\.\d+)\s+' + \
             r'(?P<meta_duration>\d+\.\d+)' + \
             r'(?:\s+(?P<meta_info>.*))?' + \
             r')'
_META_END = r'(?:^(?P<meta_end>[=]+)$)'
_LS_L_HDR = r'(?:(?P<set_name>[^:]+): .* last update: (?P<ts>.*))'
_LS_L_DATA = r'(?:(?P<F>.) (?P<type>\S+)\s+(?P<metric_name>\S+)\s+' \
             r'(?P<metric_value>.*))'
_LS_RE = re.compile(
            _META_BEGIN + "|" +
            _META_DASHES + "|" +
            _META_DATA + "|" +
            _META_SUMMARY + "|" +
            _META_END + "|" +
            _LS_L_HDR + "|" +
            _LS_L_DATA
         )
_TYPE_FN = {
    "char": lambda x: str(x).strip("'"),
    "char[]": lambda x: str(x).strip('"'),

    "u8": int,
    "s8": int,
    "u16": int,
    "s16": int,
    "u32": int,
    "s32": int,
    "u64": int,
    "s64": int,
    "f32": float,
    "d64": float,

    "u8[]": lambda x: list( int(a, base=0) for a in  x.split(',') ),
    "s8[]": lambda x: list(map(int, x.split(','))),
    "u16[]": lambda x: list(map(int, x.split(','))),
    "s16[]": lambda x: list(map(int, x.split(','))),
    "u32[]": lambda x: list(map(int, x.split(','))),
    "s32[]": lambda x: list(map(int, x.split(','))),
    "u64[]": lambda x: list(map(int, x.split(','))),
    "s64[]": lambda x: list(map(int, x.split(','))),
    "f32[]": lambda x: list(map(float, x.split(','))),
    "d64[]": lambda x: list(map(float, x.split(','))),
}

def parse_ldms_ls(txt):
    """Parse output of `ldms_ls -l [-v]` into { SET_NAME : SET_DICT } dict

    Each SET_DICT is {
        "name" : SET_NAME,
        "ts" : UPDATE_TIMESTAMP_STR,
        "meta" : {
            "schema"    :  SCHEMA_NAME,
            "instance"  :  INSTANCE_NAME,
            "flags"     :  FLAGS,
            "meta_sz"   :  META_SZ,
            "data_sz"   :  DATA_SZ,
            "uid"       :  UID,
            "gid"       :  GID,
            "perm"      :  PERM,
            "update"    :  UPDATE_TIME,
            "duration"  :  UPDATE_DURATION,
            "info"      :  APP_INFO,
        },
        "data" : {
            METRIC_NAME : METRIC_VALUE,
            ...
        },
        "data_type" : {
            METRIC_NAME : METRIC_TYPE,
        },
    }
    """
    ret = dict()
    lines = txt.splitlines()
    itr = iter(lines)
    meta_section = False
    for l in itr:
        l = l.strip()
        if not l: # empty line, end of set
            lset = None
            data = None
            meta = None
            continue
        m = _LS_RE.match(l)
        if not m:
            raise RuntimeError("Bad line format: {}".format(l))
        m = m.groupdict()
        if m["meta_begin"]: # start meta section
            if meta_section:
                raise RuntimeError("Unexpected meta info: {}".format(l))
            meta_section = True
            continue
        elif m["meta_schema"]: # meta data
            if not meta_section:
                raise RuntimeError("Unexpected meta info: {}".format(l))
            meta = dict( schema = m["meta_schema"],
                         instance = m["meta_inst"],
                         flags = m["meta_flags"],
                         meta_sz = m["meta_msize"],
                         data_sz = m["meta_dsize"],
                         uid = m["meta_uid"],
                         gid = m["meta_gid"],
                         perm = m["meta_perm"],
                         update = m["meta_update"],
                         duration = m["meta_duration"],
                         info = m["meta_info"],
                    )
            _set = ret.setdefault(m["meta_inst"], dict())
            _set["meta"] = meta
            _set["name"] = m["meta_inst"]
        elif m["meta_total_sets"]: # the summary line
            if not meta_section:
                raise RuntimeError("Unexpected meta info: {}".format(l))
            # else do nothing
            continue
        elif m["meta_dashes"]: # dashes
            if not meta_section:
                raise RuntimeError("Unexpected meta info: {}".format(l))
            continue
        elif m["meta_end"]: # end meta section
            if not meta_section:
                raise RuntimeError("Unexpected meta info: {}".format(l))
            meta_section = False
            continue
        elif m["set_name"]: # new set
            if meta_section:
                raise RuntimeError("Unexpected data info: {}".format(l))
            data = dict() # placeholder for metric data
            data_type = dict() # placeholder for metric data type
            lset = ret.setdefault(m["set_name"], dict())
            lset["name"] = m["set_name"]
            lset["ts"] = m["ts"]
            lset["data"] = data
            lset["data_type"] = data_type
        elif m["metric_name"]: # data
            if meta_section:
                raise RuntimeError("Unexpected data info: {}".format(l))
            if m["type"] == "char[]":
                _val = m["metric_value"]
            else:
                _val = m["metric_value"].split(' ', 1)[0] # remove units
            mname = m["metric_name"]
            mtype = m["type"]
            data[mname] = _TYPE_FN[mtype](_val)
            data_type[mname] = mtype
        else:
            raise RuntimeError("Unable to process line: {}".format(l))
    return ret

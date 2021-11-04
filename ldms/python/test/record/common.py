#!/usr/bin/python3

from ovis_ldms import ldms

ITEM_COUNT = 3
ARRAY_COUNT = 8

SAMP_PORT = 10000
AGG_PORT  = 20000

REC_DEF = ldms.RecordDef("device_record", metric_list = [
        (       "LDMS_V_CHAR",       ldms.V_CHAR,           1 ),
        (         "LDMS_V_U8",         ldms.V_U8,           1 ),
        (         "LDMS_V_S8",         ldms.V_S8,           1 ),
        (        "LDMS_V_U16",        ldms.V_U16,           1 ),
        (        "LDMS_V_S16",        ldms.V_S16,           1 ),
        (        "LDMS_V_U32",        ldms.V_U32,           1 ),
        (        "LDMS_V_S32",        ldms.V_S32,           1 ),
        (        "LDMS_V_U64",        ldms.V_U64,           1 ),
        (        "LDMS_V_S64",        ldms.V_S64,           1 ),
        (        "LDMS_V_F32",        ldms.V_F32,           1 ),
        (        "LDMS_V_D64",        ldms.V_D64,           1 ),
        ( "LDMS_V_CHAR_ARRAY", ldms.V_CHAR_ARRAY, ARRAY_COUNT ),
        (   "LDMS_V_U8_ARRAY",   ldms.V_U8_ARRAY, ARRAY_COUNT ),
        (   "LDMS_V_S8_ARRAY",   ldms.V_S8_ARRAY, ARRAY_COUNT ),
        (  "LDMS_V_U16_ARRAY",  ldms.V_U16_ARRAY, ARRAY_COUNT ),
        (  "LDMS_V_S16_ARRAY",  ldms.V_S16_ARRAY, ARRAY_COUNT ),
        (  "LDMS_V_U32_ARRAY",  ldms.V_U32_ARRAY, ARRAY_COUNT ),
        (  "LDMS_V_S32_ARRAY",  ldms.V_S32_ARRAY, ARRAY_COUNT ),
        (  "LDMS_V_U64_ARRAY",  ldms.V_U64_ARRAY, ARRAY_COUNT ),
        (  "LDMS_V_S64_ARRAY",  ldms.V_S64_ARRAY, ARRAY_COUNT ),
        (  "LDMS_V_F32_ARRAY",  ldms.V_F32_ARRAY, ARRAY_COUNT ),
        (  "LDMS_V_D64_ARRAY",  ldms.V_D64_ARRAY, ARRAY_COUNT ),
    ])

SCHEMA = ldms.Schema(
            name = "schema",
            metric_list = [
                ( "component_id", "u64",  1, True ),
                (       "job_id", "u64",  1  ),
                (       "app_id", "u64",  1  ),
                (        "round", "u32",  1  ),
                REC_DEF,
                ( "device_list", "list", ITEM_COUNT * REC_DEF.heap_size() ),
            ],
         )

VAL = {
        ldms.V_CHAR: lambda i: bytes( [97 + (i%2)] ).decode(),
        ldms.V_U8:   lambda i: i & 0xFF,
        ldms.V_S8:   lambda i: -(i % 128),
        ldms.V_U16:  lambda i: i + 1000,
        ldms.V_S16:  lambda i: -(i + 1000),
        ldms.V_U32:  lambda i: i + 100000,
        ldms.V_S32:  lambda i: -(i + 100000),
        ldms.V_U64:  lambda i: i + 200000,
        ldms.V_S64:  lambda i: -(i + 200000),
        ldms.V_F32:  lambda i: float(i),
        ldms.V_D64:  lambda i: float(i),
        ldms.V_CHAR_ARRAY: lambda i: "a_{}".format(i),
        ldms.V_U8_ARRAY:   lambda i: [ (i+j)&0xFF for j in range(ARRAY_COUNT)],
        ldms.V_S8_ARRAY:   lambda i: [ -((i+j)%128) for j in range(ARRAY_COUNT)],
        ldms.V_U16_ARRAY:  lambda i: [ 1000+(i+j) for j in range(ARRAY_COUNT)],
        ldms.V_S16_ARRAY:  lambda i: [ -(1000+(i+j)) for j in range(ARRAY_COUNT)],
        ldms.V_U32_ARRAY:  lambda i: [ 100000+(i+j) for j in range(ARRAY_COUNT)],
        ldms.V_S32_ARRAY:  lambda i: [ -(100000+(i+j)) for j in range(ARRAY_COUNT)],
        ldms.V_U64_ARRAY:  lambda i: [ 500000+(i+j) for j in range(ARRAY_COUNT)],
        ldms.V_S64_ARRAY:  lambda i: [ -(500000+(i+j)) for j in range(ARRAY_COUNT)],
        ldms.V_F32_ARRAY:  lambda i: [ 0.5+i+j for j in range(ARRAY_COUNT)],
        ldms.V_D64_ARRAY:  lambda i: [ 0.75+i+j for j in range(ARRAY_COUNT)],
}

def sample_set(_set, _round):
    _set.transaction_begin()
    _set["round"] = _round
    _lst = _set["device_list"]
    if len(_lst) == 0:
        # allocate records
        for i in range(ITEM_COUNT):
            _rec = _set.record_alloc("device_record")
            _lst.append(ldms.V_RECORD_INST, _rec)
    i = 0
    for rec in _lst:
        for j in range(len(rec)):
            t = rec.get_metric_type(j)
            v = VAL[t](_round + i)
            rec[j] = v
        i += 1
    _set.transaction_end()

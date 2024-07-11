===================
ldmsd_decomposition
===================

:Date: 2 Jun 2022

.. contents::
   :depth: 3
..

NAME
====================

ldmsd_decomposition - manual for LDMSD decomposition

DESCRIPTION
===========================

A decomposition is a routine that converts LDMS set into one or more
rows before feeding them to the store. Currently, only **store_sos**,
**store_csv**, and **store_kafka** support decomposition. To use
decomposition, simply specify
**decomposition=**\ *DECOMP_CONFIG_JSON_FILE* option in the
**strgp_add** command. There are three types of decompositions:
**static**, **as_is**, and \`flex\`. **static** decomposition statically
and strictly decompose LDMS set according to the definitions in the
*DECOMP_CONFIG_JSON_FILE*. **as_is** decomposition on the other hand
takes all metrics and converts them as-is into rows. **flex**
decomposition applies various decompositions by LDMS schema digest
mapping from the configuration.

Please see section **STATIC DECOMPOSITION**, **AS_IS DECOMPOSITION** ,
and **FLEX DECOMPOSITION** for more information.

More decomposition types may be added in the future. The decomposition
mechanism is pluggable. Please see **as_is**, **static**, and **flex**
decomposition implementation in \`ldms/src/decomp/\` directory in the
source tree for more information.

STATIC DECOMPOSITION
====================================

The **static** decomposition statically and strictly converts LDMS set
to one or more rows according to the *DECOMP_CONFIG_JSON_FILE*. The
format of the JSON configuration file is as follows:

::

   {
     "type": "static",
     "rows": [
       {
         "schema": "OUTPUT_ROW_SCHEMA",
         "cols": [
           { "src":"LDMS_METRIC_NAME", "dst":"OUTPUT_COL_NAME","type":"TYPE",
             "array_len": ARRAY_LEN_IF_TYPE_IS_ARRAY,
             "rec_member": "REC_MEMBER_NAME_IF_SRC_IS_RECORD",
             "fill": "FILL_VALUE"
           },
           ...
         ],
         "indices": [
           { "name":"INDEX_NAME", "cols":[ OUTPUT_COLUMNS, ... ] },
           ...
         ]
       },
       ...
     ]
   }

The "rows" is an array of row definition object, each of which defines
an output row. The "schema" attribute specifies the output schema name,
which is the schema name used by the storage plugin to identify the row
schema. Each row definition contains "cols" which is a list of column
definitions, and "indices" which is a list of index definitions. Each
column definition is an object with at least "src" describing the metric
name, "dst" describing the output column name, and "type" describing the
value type of the column. If the type is an array, "array_len" is
required. If the "src" is a list of record, "rec_member" is required to
specify the record member for the output column. The "fill" value is
used to fill in the output column in the case that the "src" metric is
not present in the LDMS set (e.g. in the case of meminfo).

Each index definition object contains "name" (the name of the index) and
"cols" which is the names of the OUTPUT columns comprising the index.

The **"timestamp"**, **"producer"**, and **"instance"** are special
"src" that refer to update timestamp, producer name and instance name of
the set respectively.

The following is an example of a static decomposition definition
converting meminfo set into two schemas, "meminfo_filter" (select a few
metrics) and "meminfo_directmap" (select a few direct map metrics with
"fill" since DirectMap varies by CPU architecture).

::

   {
     "type": "static",
     "rows": [
       {
         "schema": "meminfo_filter",
         "cols": [
           { "src":"timestamp",    "dst":"ts",      "type":"ts"                         },
           { "src":"producer",     "dst":"prdcr",   "type":"char_array", "array_len":64 },
           { "src":"instance",     "dst":"inst",    "type":"char_array", "array_len":64 },
           { "src":"component_id", "dst":"comp_id", "type":"u64"                        },
           { "src":"MemFree",      "dst":"free",    "type":"u64"                        },
           { "src":"MemActive",    "dst":"active",  "type":"u64"                        }
         ],
         "indices": [
           { "name":"time_comp", "cols":["ts", "comp_id"] },
           { "name":"time", "cols":["ts"] }
         ]
       },
       {
         "schema": "meminfo_directmap",
         "cols": [
           { "src":"timestamp",    "dst":"ts",          "type":"ts"               },
           { "src":"component_id", "dst":"comp_id",     "type":"u64"              },
           { "src":"DirectMap4k",  "dst":"directmap4k", "type":"u64",   "fill": 0 },
           { "src":"DirectMap2M",  "dst":"directmap2M", "type":"u64",   "fill": 0 },
           { "src":"DirectMap4M",  "dst":"directmap4M", "type":"u64",   "fill": 0 },
           { "src":"DirectMap1G",  "dst":"directmap1G", "type":"u64",   "fill": 0 }
         ],
         "indices": [
           { "name":"time_comp", "cols":["ts", "comp_id"] },
           { "name":"time", "cols":["ts"] }
         ]
       }
     ]
   }

The following is an example of a static decomposition with "rec_member"
usage.

::

   {
     "type": "static",
     "rows": [
       {
         "schema": "netdev2_small",
         "cols": [
           { "src":"timestamp",    "dst":"ts",      "type":"ts"                         },
           { "src":"producer",     "dst":"prdcr",   "type":"char_array", "array_len":64 },
           { "src":"instance",     "dst":"inst",    "type":"char_array", "array_len":64 },
           { "src":"component_id", "dst":"comp_id", "type":"u64"                        },
           { "src":"netdev_list",  "rec_member":"name",
             "dst":"netdev.name", "type":"char_array", "array_len":16 },
           { "src":"netdev_list",  "rec_member":"rx_bytes",
             "dst":"netdev.rx_bytes", "type":"u64" },
           { "src":"netdev_list",  "rec_member":"tx_bytes",
             "dst":"netdev.tx_bytes", "type":"u64" },
         ],
         "indices": [
           { "name":"time_comp", "cols":["ts", "comp_id"] },
           { "name":"time", "cols":["ts"] }
         ]
       }
     ]
   }

In this case, if the "netdev_list" has N members, the decomposition will
expand the set into N rows.

AS_IS DECOMPOSITION
===================================

The **as_is** decomposition generate rows as-is according to metrics in
the LDMS set. To avoid schema conflict, such as meminfo collecting from
heterogeneous CPU architectures, **as_is** decomposition appends the
short LDMS schema digest (7 characters) to the row schema name before
submitting the rows to the storage plugin. For example, "meminfo" LDMS
schema may turn into "meminfo_8d2b8bd" row schema. The **as_is**
decomposition configuration only takes "indices" attribute which defines
indices for the output rows. When encountering a list of primitives, the
as_is decomposition expands the set into multiple rows (the non-list
metrics' values are repeated). When encountering a list of records, in
addition to expanding rows, the decomposition also expand the record
into multiple columns with the name formatted as
"LIST_NAME.REC_MEMBER_NAME". The "timestamp" is not a metric in the set
but it is used in all storage plugins. So, the "timestamp" column is
prepended to each of the output rows.

The format of the JSON configuration is as follows:

::

   {
     "type": "as_is",
     "indices": [
       { "name": "INDEX_NAME", "cols": [ COLUMN_NAMES, ... ] },
       ...
     ]
   }

The following is an **as_is** decomposition configuration example with
two indices:

::

   {
     "type": "as_is",
     "indices": [
       { "name": "time", "cols": [ "timestamp" ] },
       { "name": "time_comp", "cols": [ "timestamp", "component_id" ] }
     ]
   }

FLEX DECOMPOSITION
==================================

The **flex** decomposition applies various decompositions by LDMS schema
digests specified in the configuration. The configurations of the
applied decompositions are also specified in \`flex\` decomposition file
as follows:

::

   {
     "type": "flex",
     /* defining decompositions to be applied */
     "decomposition": {
       "<DECOMP_1>": {
         "type": "<DECOMP_1_TYPE>",
         ...
       },
       ...
     },
     /* specifying digests and the decompositions to apply */
     "digest": {
       "<LDMS_DIGEST_1>": "<DECOMP_A>",
       "<LDMS_DIGEST_2>": [ "<DECOMP_B>", "<DECOMP_c>" ],
       ...
       "*": "<DECOMP_Z>" /* optional : the unmatched */
     }
   }

**Example:** In the following example, the "meminfo" LDMS sets have 2
digests due to different metrics from different architecture. The
configuration then maps those digests to "meminfo" static decomposition
(producing "meminfo_filter" rows). It also showcases the ability to
apply multiple decompositions to a matching digest. The procnetdev2 sets
with digest
"E8B9CC8D83FB4E5B779071E801CA351B69DCB9E9CE2601A0B127A2977F11C62A" will
have "netdev2" static decomposition and "the_default" as-is
decomposition applied to them. The sets that do not match any specific
digest will match the "\*" digest. In this example, "the_default" as-is
decomposition is applied.

::

   {
     "type": "flex",
     "decomposition": {
       "meminfo": {
         "type": "static",
         "rows": [
           {
             "schema": "meminfo_filter",
             "cols": [
               { "src":"timestamp",    "dst":"ts",      "type":"ts"                         },
               { "src":"producer",     "dst":"prdcr",   "type":"char_array", "array_len":64 },
               { "src":"instance",     "dst":"inst",    "type":"char_array", "array_len":64 },
               { "src":"component_id", "dst":"comp_id", "type":"u64"                        },
               { "src":"MemFree",      "dst":"free",    "type":"u64"                        },
               { "src":"MemActive",    "dst":"active",  "type":"u64"                        }
             ],
             "indices": [
               { "name":"time_comp", "cols":["ts", "comp_id"] },
               { "name":"time", "cols":["ts"] }
             ]
           }
         ]
       },
       "netdev2" : {
         "type" : "static",
         "rows": [
           {
             "schema": "procnetdev2",
             "cols": [
               { "src":"timestamp", "dst":"ts","type":"ts" },
               { "src":"component_id", "dst":"comp_id","type":"u64" },
               { "src":"netdev_list", "rec_member":"name", "dst":"dev.name",
                 "type":"char_array", "array_len": 16 },
                 { "src":"netdev_list", "rec_member":"rx_bytes", "dst":"dev.rx_bytes",
                   "type":"u64" },
                   { "src":"netdev_list", "rec_member":"tx_bytes", "dst":"dev.tx_bytes",
                     "type":"u64" }
             ],
             "indices": [
               { "name":"time_comp", "cols":["ts", "comp_id"] }
             ]
           }
         ]
       },
       "the_default": {
         "type": "as_is",
         "indices": [
           { "name": "time", "cols": [ "timestamp" ] },
           { "name": "time_comp", "cols": [ "timestamp", "component_id" ] }
         ]
       }
     },
     "digest": {
       "71B03E47E7C9033E359DB5225BC6314A589D8772F4BC0866B6E79A698C8799C0": "meminfo",
       "59DD05D768CFF8F175496848486275822A6A9795286FD9B534FDB9434EAF4D50": "meminfo",
       "E8B9CC8D83FB4E5B779071E801CA351B69DCB9E9CE2601A0B127A2977F11C62A": [ "netdev2", "the_default" ],
       "*": "the_default"
     }
   }

SEE ALSO
========================

Plugin_store_sos(7), Plugin_store_csv(7), Plugin_store_kafka(7)

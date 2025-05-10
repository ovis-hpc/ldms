.. _ldmsd_decomposition:

===================
ldmsd_decomposition
===================

------------------------------
Manual for LDMSD decomposition
------------------------------

:Date: 2022-06-02
:Version: v4
:Manual section: 7
:Manual group: LDMSD Decomposition man page

DESCRIPTION
===========

A decomposition is a routine that converts LDMS set into one or more
rows before feeding them to the store. Currently, only **store_sos**,
**store_csv**, and **store_kafka** support decomposition. To use
decomposition, simply specify
**decomposition=**\ *DECOMP_CONFIG_JSON_FILE* option in the
**strgp_add** command. There are three types of decompositions:
**static**, **as_is**, and \`flex`. **static** decomposition statically
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
====================

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
           {
             "src":"LDMS_METRIC_NAME",
             "dst":"DST_COL_NAME",
             "type":"TYPE",
             "rec_member": "REC_MEMBER_NAME_IF_SRC_IS_RECORD",
             "fill": FILL_VALUE,
             "op": "OPERATION"
           },
           ...
         ],
         "indices": [
           {
             "name": "INDEX_NAME",
             "cols": [
               "DST_COL_NAME", ...
             ]
           },
           ...
         ],
         "group": {
           "limit": ROW_LIMIT,
           "index": [
             "DST_COL_NAME", ...
           ],
           "order": [
             "DST_COL_NAME", ...
           ],
           "timeout": "TIME"
         }
       },
       ...
     ]
   }

The **"rows"** is an array of row definition object, each of which
defines an output row. Each row definition contains:

-----------------------------------------------------
**"schema"**: a string specifying output schema name,
-------------------------------------------------------------------------------------------------------------
**"cols"**: a list of column definitions,    | - **"indices"**: an optional list of index definitions for the
-------------------------------------------------------------------------------------------------------------

     storage technologies that require or support indexing, and
   | - **"group"**: a grouping definition for "op" operations ("group"
     is not required if "op" is not specified; see "op" and "group"
     below).

The detail explanation of **"cols"**, **"indices"** and **"group"** are
as follows.

**"cols"**
   Each column object in **"cols"** contains the following attributes:

..

   **"src"** : "*LDMS_METRIC_NAME*"
      This refers to the metric name in the LDMS set to be used as the
      source of the decomposition. *LDMS_METRIC_NAME* can also be
      specified in the form of "*LIST*\ [*MEMBER*]" to refer to MEMBER
      of the record in the list NAME. For example,

     ::

          "src" : "netdev_list[rx_bytes]"

     refers to the "rx_bytes" member of records in "netdev_list".

     special src
       The following is a list of special metric names that can be used in "src"
       to access set information as column data:

       | - **"timestamp"**: the sampling timestamp.
       | - **"producer"**: the producer name of the set.
       | - **"instance"**: the instance name of the set.
       | - **"M_card"**: the cardinality of the set.
       | - **"M_digest"**: the digest string of the set schema.
       | - **"M_duration"**: the sampling duration of the set.
       | - **"M_gid"**: the set's owner GID.
       | - **"M_instance"**: the instance name of the set (same as "instance").
       | - **"M_perm"**: the integer value of the permission of the set.
       | - **"M_producer"**: the producer name of the set (same as "producer").
       | - **"M_schema"**: the schema name of the set.
       | - **"M_timestamp"**: the sampling timestamp (same as "timestamp").
       | - **"M_uid"**: the set's owner UID.

   **"dst"** : "*DST_COL_NAME*" (optional)
      This is the name of the output column, later consumed by storage
      policy. If not specified, the *LDMS_METRIC_NAME* specified in
      "src" is used.

   **"type"** : "*TYPE*" (required if "fill" is specified)
      The type of the output column. This is required if "fill"
      attribute if specified. If "fill" is not specified, "type" is
      optional. In such case, the type is the first discovery from the
      metric value in the LDMS set processed by this decomposition.

   **"rec_member"** : "*MEMBER_NAME*" (optional)
      If "src" refers to a list of records or an array of records,
      "rec_member" can be specified to access the member of the records.
      Alternatively, you can use "*LIST*\ [*MEMBER*]" form in "src" to
      access the member in the records.

   **"fill"** : *FILL_VALUE* (optional)
      This is the value used to fill in place of "src" in the case that
      the LDMS set does not contain "src" metric. The *FILL_VALUE* can
      also be an array. If "src" is not found in the LDMS set and "fill"
      is not specified, the LDMS set is skipped.

   **"op"** : "*OPERATION*" (optional)
      If "op" is set, the decomposition performs the specified
      *OPERATION* on the column. **"group"** must be specified in the
      presence of "op" so that the decomposition knows how to group
      previously produced rows and perform the operation on the column
      of those rows. Please see **"group"** explanation below.

   The supported *OPERATION* are "diff", "min", "max", and "mean".

**"indices"**
   The "indices" is a list of index definition objects. Each index
   definition object contains **"name"** (the name of the index) and
   **"cols"** which is the names of the OUTPUT columns comprising the
   index.

**"group"**
   The **"group"** is an object defining how **"op"** identify rows to
   operate on. The **REQUIRED** attributes and their descriptions for
   the **"group"** object are as follows:

..

   **"index"** : [ "*DST_COL_NAME*", ... ]
      This is a list of columns that defines the grouping index. If two
      rows r0 and r1 have the same value in each of the corresponding
      columns, i.e. for k in index: r0[k] == r1[k], the rows r0 and r1
      belong to the same group.

   **"order"** : [ "*DST_COL_NAME*", ... ]
      This is a list of columns used for orering rows in each group (in
      descending order). For example, \`[ "timestamp" ]\` orders each
      group (in descending order) using "timestamp" column.

   **"limit"** : *ROW_LIMIT*
      This is an integer limiting the maximum number of rows to be
      cached in each group. The first *ROW_LIMIT* rows in the group
      descendingly ordered by **"order"** are cached. The rest are
      discarded.

   **"timeout"** : "*TIME*"
      The amount of time (e.g. "30m") of group inactivity (no row added
      to the group) to trigger row cache cleanup for the group. If this
      value is not set, the row cache won't be cleaned up.

**Static Decomposition Example 1: simple meminfo with fill**
   The following is an example of a static decomposition definition
   converting meminfo set into two schemas, "meminfo_filter" (select a
   few metrics) and "meminfo_directmap" (select a few direct map metrics
   with "fill" since DirectMap varies by CPU architecture).

::

   {
     "type": "static",
     "rows": [
       {
         "schema": "meminfo_filter",
         "cols": [
           { "src":"timestamp",    "dst":"ts"      },
           { "src":"producer",     "dst":"prdcr"   },
           { "src":"instance",     "dst":"inst"    },
           { "src":"component_id", "dst":"comp_id" },
           { "src":"MemFree",      "dst":"free"    },
           { "src":"MemActive",    "dst":"active"  }
         ],
         "indices": [
           { "name":"time_comp", "cols":["ts", "comp_id"] },
           { "name":"time", "cols":["ts"] }
         ]
       },
       {
         "schema": "meminfo_directmap",
         "cols": [
           { "src":"timestamp",    "dst":"ts"                                     },
           { "src":"component_id", "dst":"comp_id"                                },
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

**Static Decomposition Example 2: record with op**
   The following is an example of a static decomposition with
   "rec_member" usage in various forms and with "op".

::

   {
     "type": "static",
     "rows": [
       {
         "schema": "netdev2_small",
         "cols": [
           { "src":"timestamp",             "dst":"ts",             "type":"ts"         },
           { "src":"producer",              "dst":"prdcr",          "type":"char_array" },
           { "src":"instance",              "dst":"inst",           "type":"char_array" },
           { "src":"component_id",          "dst":"comp_id",        "type":"u64"        },
           { "src":"netdev_list",           "rec_member":"name",    "dst":"netdev.name" },
           { "src":"netdev_list[rx_bytes]", "dst":"netdev.rx_bytes" },
           { "src":"netdev_list[tx_bytes]"  },
           { "src":"netdev_list[rx_bytes]", "op": "diff",
             "dst":"netdev.rx_bytes_diff" },
           { "src":"netdev_list[tx_bytes]", "op": "diff",
             "dst":"netdev.tx_bytes_diff" }
         ],
         "indices": [
           { "name":"time_comp", "cols":["ts", "comp_id"] },
           { "name":"time", "cols":["ts"] }
         ],
         "group": [
           "limit": 2,
           "index": [ "comp_id", "netdev.name" ],
           "order": [ "ts" ],
           "timeout": "60s"
         ]
       }
     ]
   }

The "name" record member will produce "netdev.name" column name and
"rx_bytes" record member will produce "netdev.rx_bytes" column name as
instructed, while "tx_bytes" will produce "netdev_list[tx_bytes]" column
name since its "dst" is omitted.

The "netdev.rx_bytes_diff" destination column has "op":"diff" that
calculate the difference value from "src":"netdev_list[rx_bytes]". The
"group" instructs "op" to group rows by ["comp_id", "netdev.name"], i.e.
the "diff" will be among the same net device of the same node (comp_id).
The "order":["ts"] orders the rows in the group by "ts" (the timestamp).
The "limit":2 keeps only 2 rows in the group (current and previous row
by timestamp). The "timeout": "60s" indicates that if a group does not
receive any data in 60 seconds (e.g. by removing a virtual network
device), the row cache for the group will be cleaned up.

The "netdev.tx_bytes_diff" is the same as "netdev.rx_bytes_diff" but for
tx_bytes.

Assuming that the "netdev_list" has N records in the list, the
decomposition will expand the set into N rows.

AS_IS DECOMPOSITION
===================

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
==================

The **flex** decomposition applies various decompositions by LDMS schema
digests or `matches` specified in the configuration. The configurations of the
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
     },
     /* specifying matching conditions and decompositions to apply */
     "matches": [
       {
         "schema": "<REGEX>", /* schema matching */
         "instance": "<REGEX>", /* instance matching */

         /* If both "schema" and "instance" are specified, a set must
          * satisfies both conditions.
          */

         /* specifying decompositions to apply to the matched set */
         "apply": "<DECOMP_X>"|[ "DECOMP_A", ...]
       },
       ...
     ],
     /* Optional "default" decompositions if a set does not match any "matches"
      * or any digest in the "digest" section. */
     "default": "<DECOMP_X>"|[ "DECOMP_A", ...]
   }

**Example1:** In the following example, the "meminfo" LDMS sets have 2
digests due to different metrics from different architecture. The
configuration then maps those digests to "meminfo" static decomposition
(producing "meminfo_filter" rows). It also showcases the ability to
apply multiple decompositions to a matching digest. The procnetdev2 sets
with digest
"E8B9CC8D83FB4E5B779071E801CA351B69DCB9E9CE2601A0B127A2977F11C62A" will
have "netdev2" static decomposition and "the_default" as-is
decomposition applied to them. The sets that do not match any specific
digest will match the "*" digest. In this example, "the_default" as-is
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


**Example2:** This is another example with the same setup as Example1, but we
use "matches" with "schema" instead of "digest".

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
     "matches": [
       { "schema": "meminfo", "apply": "meminfo" },
       { "schema": "procnetdev2", "apply": [ "netdev2", "the_default" ] }
     ],
     "default": "the_default"
   }

SEE ALSO
========

:ref:`store_sos(7) <store_sos`, :ref:`store_csv(7) <store_csv>`, :ref:`store_kafka(7) <store_kafka>`

.. _json_stream_sampler:

==========================
json_stream_sampler
==========================

-------------------------------------------------
Man page for the LDMSD json_stream_sampler plugin
-------------------------------------------------

:Date:   5 Aug 2023
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

Within ldmsd_controller or a configuration file:

**config** **name=\ json_stream_sampler** **producer=\ PRODUCER**
**instance=\ INSTANCE** [ **component_id=\ COMP_ID** ] [
**stream=\ NAME** ] [ **uid=\ UID** ] [ **gid=\ GID** ] [
**perm=\ PERM** ] [ **heap_szperm=\ BYTES** ]

DESCRIPTION
===========

The **json_stream_store** monitors JSON object data presented on a
configured set of streams. JSON object data is encoded in LDMS Metric
Sets; the intention of which is to store these metric sets using
decomposition through a storage plugin.

When publishing JSON dictionary data to **json_stream_plugin**, there
are fields in the JSON dictionary that have special meaning. These
fields are shown in the table below:

+--------------------+---------------+-------------------------------+
| **Attribute Name** | **Data Type** | **Description**               |
+====================+===============+===============================+
| schema             | string        | The name of a Metric Set      |
|                    |               | schema for JSON dictionaries  |
|                    |               | received on this stream.      |
+--------------------+---------------+-------------------------------+
| *NAME*\ \_max_len  | integer       | For a list or array named     |
|                    |               | *NAME*, this is maximum       |
|                    |               | length of the list or array.  |
+--------------------+---------------+-------------------------------+

Schema Management
-----------------

The value of the *schema* attribute in the top-level JSON dictionary is
maintained in a tree. The first time the schema name is seen, an LDMS
Schema is created based on the value of the JSON dictionary. Once
created, the schema is used to create the metric set. Each time a stream
message is received, the metric set is updated.

The *schema* attribute is mandatory. If it not present in the top-level
JSON dictionary, an error is logged and the message is ignored.

Encoding Types
--------------

Primitive types are encoded as attributes in the LDMS metric set with
their associated LDMS type. The table below shows how the JSON
attributes are mapped to LDMS metric types.

+----------------+-------------------+-------------------------------------------+
| **JSON Type**  | **LDMS Type**     | **Example JSON Value**                    |
+================+===================+===========================================+
| Integer        | LDMS_V_S64        | 45                                        |
+----------------+-------------------+-------------------------------------------+
| Floating Point | LDMS_V_D64        | 3.1415                                    |
+----------------+-------------------+-------------------------------------------+
| String         | LDMS_V_BYTE_ARRAY | "hello", 'world'                          |
+----------------+-------------------+-------------------------------------------+
| List           | LDMS_V_LIST       | [ 1, 2, 3 ]                               |
+----------------+-------------------+-------------------------------------------+
| Dictionary     | LDMS_V_RECORD     | { "attr1" : 1, "attr2" : 2, "attr3" : 3 } |
+----------------+-------------------+-------------------------------------------+

The encoding of all JSON types except strings, dictionaries and lists is
straightfoward. The coding of Strings, Lists and Dictionaries have
additional limitations as described below.

Stream Meta-data
----------------

Stream events include the user-id, and group-id of the application
publishing the stream data. This data is encoded in the metric set with
the special names **S_uid**, and **S_gid** respectively. The intention
is that this data can stored in rows as configured by the user with a
decomposition configuration.

Encoding Strings
----------------

Strings are encoded as LDMS_V_BYTE_ARRAY. By default, the length of the
array is 255 unless an attribute with the name *NAME*\ \_max_len is
present in the dictionary along with the string value, its value is used
to size the string array.

For example:

   ::

      { "my_string" : "this is a string", "my_string_max_len" : 4096 }

will result in an LDMS metric with the name "my_string", type
LDMS_V_BYTE_ARRAY, and length of 4096 being created in the metric set.

Encoding Arrays
---------------

Any list present in the top-level dictionary is encoded as a list,
however, lists present in a 2nd-level dictionary are encoded as arrays.
This is because LDMS_V_LIST inside an LDMS_V_RECORD is not supported.
The length of the array is determined by the initial value of the array
in the record; but can be overridden with the *NAME*\ \_max_len
attribute as described above for strings. Lists of strings in a
2nd-level dictionary are treated as a JSON-formatted string of a list.
That is, they are encoded as LDMS_V_CHAR_ARRAY because LDMS does not
support arrays of LDMS_V_CHAR_ARRAY. The length of the array is
determined by the length of the JSON-formatted string of the initial
list.

Encoding Dictionaries
---------------------

The attributes in the top-level JSON dictionary are encoded in the
metric set directly. For example the JSON dictionary:

   ::

      {
        "schema" : "example",
        "component_id", 10001,
        "job_id" : 2048,
        "seq" : [ 1, 2, 3 ]
      }

results in a metric set as follows:

   ::

      $ ldms_ls -h localhost -p 10411 -a munge -E example -l
      ovs-5416_example: consistent, last update: Sat Aug 05 11:38:26 2023 -0500 [281178us]
      D s32        S_uid                                      1002
      D s32        S_gid                                      1002
      D s64        component_id                               10001
      D s64        job_id                                     2048
      D list<>     seq                                        [1,2,3]
      D char[]     schema                                     "example"

Dictionaries inside the top-level dictionary are encoded as
LDMS_V_RECORD inside a single element LDMS_V_RECORD_ARRAY. This
limitation is because an LDMS_V_RECORD is only allowed inside an
LDMS_V_LIST or LDMS_V_ARRAY.

The JSON below:

   ::

      {
        "schema" : "dictionary",
        "a_dict" : { "attr_1" : 1, "attr_2" : 2 },
        "b_dict" : { "attr_3" : 3, "attr_4" : 4 }
      }

results in the following LDMS metric set.

   ::

      ovs-5416_dict: consistent, last update: Sat Aug 05 21:14:38 2023 -0500 [839029us]
      D s32         S_uid                                      1002
      D s32         S_gid                                      1002
      M record_type  a_dict_record                             LDMS_V_RECORD_TYPE
      D record[]     a_dict
        attr_2 attr_1
             2      1
      M record_type  b_dict_record                             LDMS_V_RECORD_TYPE
      D record[]     b_dict
        attr_4 attr_3
             4      3
      D char[]     schema                                     "dict"

Lists of JSON dictionaries results in each dictionary being encoded as
an element in an LDMS_V_LIST. Note that all elements in the list must be
the same type.

The JSON below:

   ::

      { "schema" : "dict_list",
        "a_dict_list" : [
          { "attr_1" : 1, "attr_2" : 2 },
          { "attr_1" : 3, "attr_2" : 4 }
        ]
      }

results in the following LDMS metric set.

   ::

      ovs-5416_dict_list: consistent, last update: Sat Aug 05 21:23:11 2023 -0500 [52659us]
      D s32         S_uid                                      1002
      D s32         S_gid                                      1002
      M record_type a_dict_list_record                         LDMS_V_RECORD_TYPE
      D list<>      a_dict_list
        attr_2 attr_1
             2      1
             4      3
      D char[]     schema                                     "dict_list"

The JSON below:

   ::

      { 'schema'  : 'json_dict',
        'dict'    : { 'int'         : 10,
                      'float'       : 1.414,
                      'char'        : 'a',
                      'str'         : 'xyz',
                      'array_int'   : [5, 7, 9],
                      'array_float' : [3.14, 1.414, 1.732],
                      'array_str'   : ['foo', 'bar'],
                      'inner_dict'  : { 'This': 'is',
                                        'a' : 'string'
                                      }
                    }
      }

results in the following LDMS metric sets.

   ::

      ovis-5416_lists_inside_a_dict: consistent, last update: Mon Sep 25 16:21:35 2023 -0500 [310003us]
      D s32          S_uid                                      1000
      D s32          S_gid                                      1000
      M record_type  dict_record                                LDMS_V_RECORD_TYPE
      D record[]     dict
        int_array char       str_array    float                   inner_dict                float_array   str int
            5,7,9  "a" "["foo","bar"]" 1.414000 "{"This":"is","a":"string"}" 3.140000,1.414000,1.732000 "xyz"  10
      D char[]       schema                                     "json_dict"

Set Security
------------

The metric sets' UID, GID, and permission can be configured using the
configuration attributes uid, gid, and perm consecutively. If one is not
given, the value of the received stream data will be used at set
creation. Once a metric set has been created, the UID, GID, and
permission will not be changed automatically when the stream data's
security data gets changed. However, it could be modified via an LDMSD
configuration command, set_sec_mod. See ldmsd_controller's Man Page.

Note that the UID, GID, and permissions values given at the
configuration line do not affect the S_uid and S_gid metric values. The
S_uid and S_gid metric values are always the security embeded with the
stream data.

CONFIG OPTIONS
==============

**name=json_stream_sampler**
   This must be json_stream_sampler (the name of the plugin).

**producer=\ NAME**
   The *NAME* of the data producer (e.g. hostname).

**instance=\ NAME**
   The *NAME* of the set produced by this plugin. This option is
   required.

**component_id=\ INT**
   An integer identifying the component (default: *0*).

**stream=\ NAME**
   The name of the LDMSD stream to register for JSON object data.

**uid=\ UID**
   The user-id to assign to the metric set.

**gid=\ GID**
   The group-id to assign to the metric set.

**perm=\ OCTAL**
   An octal number specifying the read-write permissions for the metric
   set. See :ref:`open(3) <open>`.

**heap_sz=\ BYTES**
   The number of bytes to reserve for the metric set heap.

BUGS
====

Not all JSON objects can be encoded as metric sets. Support for records
nested inside other records is accomplished by encoding the nested
records as strings.

EXAMPLES
========

Plugin configuration example:

   ::

      load name=json_stream_sampler
      config name=json_stream_sampler producer=${HOSTNAME} instance=${HOSTNAME}/slurm \
             component_id=2 stream=darshan_data heap_sz=1024
      start name=json_stream_sampler interval=1000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`store_avro_kakfa(8) <store_avro_kakfa>`

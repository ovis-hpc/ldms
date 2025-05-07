.. _store_avro_kafka:

=======================
store_avro_kafka
=======================

------------------------------
LDMSD store_avro_kafka plugin
------------------------------

:Date:   30 Mar 2023
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

**config** **name=store_avro_kafka** **producer=PRODUCER**
**instance=INSTANCE** [ **topic=\ TOPIC_FMT** ] [ **encoding=\ JSON** ]
[ **encoding=\ AVRO** ] [ **kafka_conf=\ PATH** ] [**serdes_conf=\ PATH** ]

DESCRIPTION
===========

**``store_avro_kafka``** implements a decomposition capable LDMS metric
data store. The **``store_avro_kafka``** plugin does not implement the
**``store``** function and must only be used with decomposition.

The plugin operates in one of two modes: *JSON*, and *AVRO* (the
default). In *JSON* mode, each row is encoded as a JSON formatted text
string. In *AVRO* mode, each row is associated with an AVRO schema and
serialized using an AVRO Serdes.

When in *AVRO* mode, the plugin manages schema in cooperation with an
Avro Schema Registry. The location of this registry is specified in a
configuration file or optionally on the **``config``** command line.

CONFIG OPTIONS
==============

-  **mode**
   A string indicating the encoding mode: "JSON" will encode messages in
   JSON format, "AVRO" will encode messages using a schema and Avro
   Serdes. The default is "AVRO". The mode values are not case
   sensitive.

-  **name**
   Must be store_avro_kafka.

-  **kafka_conf**
   A path to a configuration file in Java property format. This
   configuration file is parsed and used to configure the Kafka
   kafka_conf_t configuration object. The format of this file and the
   supported attributes are available here:
   https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md.

-  **serdes_conf**
   A path to a configuration file in Java property format. This
   configuration file is parsed and used to configure the Avro Serdes
   serdes_conf_t configuration object. The only supported option for
   this file is serdes.schema.url.

TOPIC NAMES
===========

The topic name to which messages are published is defined by the
**topic** configuration parameter. The parameter specifies a string that
is a *format specifier* similar to a printf() format string. If the
**topic** is not specified, it defaults to "%S" which is the format
specifier for the set schema name.

The '%' character introduces a *format specifier* that will be
substituted in the topic format string to create the topic name. The
format specifiers are as follows:

-  **%F** The format in which the message is serialized: "json" or "avro".

-  **%S** The set parameter's *schema* name.

-  **%I** The instance name of the set, e.g. "orion-01/meminfo".

-  **%P** The set parameter's *producer* name, e.g. "orion-01."

-  **%u** The user-name string for the owner of the set. If the user-name is
   not known on the system, the user-id is used.

-  **%U** The user-id (uid_t) for the owner of the set.

-  **%g** The group-name string for the group of the set. If the group-name is
   not known on the system, the group-id is used.

-  **%G** The group-id (gid_t) for the group of the set.

-  **%a** The access/permission bits for the set formatted as a string, e.g.
   "-rw-rw----".

-  **%A** The access/permission bits for the set formatted as an octal number,
   e.g. 0440.

Note that a topic name must only consist of a combination of the
characters [a-zA-Z0-9\._\-]. In order to ensure that the format
specifier above will not produce invalid topic names, any character that
results from a format specifier substitution that is not in the valid
list will be substituted with a '.'.

STRGP
=====

The store_avro_kafka is used with a storage policy that specifies
store_avro_kafka as the plugin parameter.

The *schema*, *instance*, *producer* and *flush* strgp_add parameters
have no affect on how data is stored. If the *container* parameter is
set to any value other than an empty string, it will override the
bootstrap.servers Kafka configuration parameter in the kafka_conf file
if present.

JSON Mode
=========

JSON mode encodes messages as self describing text objects. Each message
is a JSON dictionary based on the following template: RS 4

::

   {
           "<attr-name-1>" : <attr-value-1>,
           "<attr-name-2>" : <attr-value-2>,
           ...
   }

Each row in the decomposition is encoded as shown. The **attr-value**
types are mapped to either quoted strings, floating-point, or integers
as defined by the source metric type in the LDMS metric set. The mapping
is as follows:

+------------------+----------------------+------------------------+
| **Metric Type**  | **Format Specifier** | **Description**        |
+------------------+----------------------+------------------------+
| LDMS_V_TIMESTAMP | %u.%06u              | Floating point number  |
|                  |                      | in seconds             |
+------------------+----------------------+------------------------+
| LDMS_V_CHAR      | %s                   | String                 |
+------------------+----------------------+------------------------+
| LDMS_V_U8        | %hhu                 | Unsigned integer       |
+------------------+----------------------+------------------------+
| LDMS_V_S8        | %hhd                 | Signed integer         |
+------------------+----------------------+------------------------+
| LDMS_V_U16       | %hu                  | Unsigned integer       |
+------------------+----------------------+------------------------+
| LDMS_V_S16       | %hd                  | Signed integer         |
+------------------+----------------------+------------------------+
| LDMS_V_U32       | %u                   | Unsigned integer       |
+------------------+----------------------+------------------------+
| LDMS_V_S32       | %d                   | Signed integer         |
+------------------+----------------------+------------------------+
| LDMS_V_U64       | %lu                  | Unsigned integer       |
+------------------+----------------------+------------------------+
| LDMS_V_S64       | %ld                  | Signed integer         |
+------------------+----------------------+------------------------+
| LDMS_V_FLOAT     | %.9g                 | Floating point         |
+------------------+----------------------+------------------------+
| LDMS_V_DOUBLE    | %.17g                | Floating point         |
+------------------+----------------------+------------------------+
| LDMS_V_STRING    | "%s"                 | Double quoted string   |
+------------------+----------------------+------------------------+
| LDMS_V_ARRAY_xxx | [ v0, v1, ... ]      | Comma separated value  |
|                  |                      | list surrounding by    |
|                  |                      | '[]'                   |
+------------------+----------------------+------------------------+

Example JSON Object
-------------------

{"timestamp":1679682808.001751,"component_id":8,"dev_name":"veth1709f8b","rx_packets":0,"rx_err_packets":0,"rx_drop_packets":0,"tx_packets":858,"tx_err_packets":0,"tx_drop_packets":0}

Avro Mode
=========

In Avro mode, LDMS metric set values are first converted to Avro values.
The table below describes how each LDMS metric set value is represented
by an Avro value.

Each row in the decomposition is encoded as a sequence of Avro values.
The target Avro type is governed by the Avro schema. The mapping is as
follows:

+-------------------+---------------+--------------------------------+
| **Metric Type**   | **Avro Type** | **LogicalType**                |
+-------------------+---------------+--------------------------------+
| LDMS_V_TIMESTAMP  | AVRO_INT64    | timestamp-millis               |
+-------------------+---------------+--------------------------------+
| LDMS_V_CHAR       | AVRO_STRING   | single-character               |
+-------------------+---------------+--------------------------------+
| LDMS_V_U8         | AVRO_INT32    | uint8                          |
+-------------------+---------------+--------------------------------+
| LDMS_V_S8         | AVRO_INT32    | int8                           |
+-------------------+---------------+--------------------------------+
| LDMS_V_U16        | AVRO_INT32    | unsigned-short                 |
+-------------------+---------------+--------------------------------+
| LDMS_V_S16        | AVRO_INT32    | signed-short                   |
+-------------------+---------------+--------------------------------+
| LDMS_V_U32        | AVRO_INT64    | unsigned-int                   |
+-------------------+---------------+--------------------------------+
| LDMS_V_S32        | AVRO_INT32    |                                |
+-------------------+---------------+--------------------------------+
| LDMS_V_U64        | AVRO_INT64    | unsigned-long                  |
+-------------------+---------------+--------------------------------+
| LDMS_V_S64        | AVRO_INT64    |                                |
+-------------------+---------------+--------------------------------+
| LDMS_V_FLOAT      | AVRO_FLOAT    |                                |
+-------------------+---------------+--------------------------------+
| LDMS_V_DOUBLE     | AVRO_DOUBLE   |                                |
+-------------------+---------------+--------------------------------+
| LDMS_V_CHAR_ARRAY | AVRO_STRING   |                                |
+-------------------+---------------+--------------------------------+
| LDMS_V_ARRAY_xxx  | AVRO_ARRAY    | Comma separated value list or  |
|                   |               | primitive type surrounded by   |
|                   |               | '[]'                           |
+-------------------+---------------+--------------------------------+

Schema Creation
---------------

Each row in the LDMS metric set presented for storage is used to
generate an Avro schema definition. The table above shows the Avro types
that are used to store each LDMS metric type. Note that currently, all
LDMS_V_TIMESTAMP values in a metric set are stored as the Avro logical
type "timestamp-millis" and encoded as an Avro long.

Unsigned types are currently encoded as signed types. The case that
could cause issues is LDMS_V_U64 which when encoded as AVRO_LONG will
result in a negative number. One way to deal with this is to encode
these as AVRO_BYTES[8] and let the consumer perform the appropriate
cast. This, however, seems identical to simply encoding it as a signed
long and allow the consumer to cast the signed long to an unsigned long.

Schema Registration
-------------------

The Avro schema are generated from the row instances presented to the
commit() storage strategy routine. The **schema_name** that is contained
in the row instance is used to search for a serdes schema. This name is
first searched for in a local RBT and if not found, the Avro Schema
Registry is consulted. If the schema is not present in the registry, a
new Avro schema is constructed per the table above, registered with the
schema registry and stored in the local cache.

Note that Avro schema names must contain only the characters
[a-zA-Z0-9\._\-], any characters in the row schema name that do not come
from this set will be forced to '.'.

A similar mapping is done for Avro value names, however, because these
names cannot accept the character '.', all invalid characters are mapped
to '_'.

These change are made automatically and no errors are generated.

Encoding
--------

After the schema is located, constructed, and or registered for the row,
the schema in conjunction with libserdes is used to binary encode the
Avro values for each column in the row. Once encoded, the message is
submitted to Kafka.

Client Side Decoding
--------------------

Consumers of topics encoded with libserdes will need to perform the
above procedure in reverse. The message received via Kafka will have the
schema-id present in the message header. The client will use this
schema-id to query the Schema registry for a schema. Once found, the
client will construct a serdes from the schema definition and use this
serdes to decode the message into Avro values.

EXAMPLES
========

kafka_conf Example File
-----------------------

   ::

      # Lines beginning with '#' are considered comments.
      # Comments and blank lines are ignored.

      # Specify the location of the Kafka broker
      bootstrap.server=localhost:9092

serdes_conf Example File
------------------------

   ::

      # Specify the location of the Avro Schema registry. This can be overridden
      # on the strgp_add line with the "container" strgp_add option if it is
      # set to anything other than an empty string
      serdes.schema.url=https://localhost:8081

Example strg_add command
------------------------

   ::

      strgp_add name=aks plugin=store_avro_kafka container=kafka-broker.int:9092 decomposition=aks-decomp.conf
      strgp_start name=aks

Example strg_add command w/o container
--------------------------------------

In this example, the strgp_add parameter, container, is set to be
ignored by store_avro_kafka. In this case, either the default,
localhost:9092, or the value specified in the rd_kafka_conf file is
used.

   ::

      strgp_add name=aks plugin=store_avro_kafka container= decomposition=aks-decomp.conf
      strgp_start name=aks

Example plugin configuration
----------------------------

   ::

      config name=store_avro_kafka encoding=avro kafka_conf=/etc/kakfa.conf serdes_conf=/etc/serdes.conf topic=ldms.%S
      strgp_start name=aks

NOTES
=====

This man page is a work in progress.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldmsd_decomposition(7) <ldmsd_decomposition>`,
:ref:`ldms_quickstart(7) <ldms_quickstart>`

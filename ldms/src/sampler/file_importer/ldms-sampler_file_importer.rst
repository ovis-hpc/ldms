.. _file_exporter

===============
 file_importer
===============

-------------------------------------------
Man page for the LDMS file_importer plugin
-------------------------------------------

:Date: 25 Aug 2025
:Manual section: 7
:Manual group: LDMS Sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:

.. code-block:: text

   config name=CFG_NAME name=file_importer [ <attr>=<value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
**ldmsd** (LDMS Daemon) are configured via **ldmsd_controller** or a
configuration file. The *file_importer* plugin searches a directory
and imports all files in the directory into metric sets. Each
file found results in its own schema and metric set instance.

The rules governing how the contents of a file are imported into a
metric set are specified in the imported file itself.

This plugin is multi-instance capable.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The file_importer plugin uses the *sampler_base* base class. This man
page covers only the configuration attributes specific to this plugin,
or those for which the behavior is different from that of
:ref:`ldms_sampler_base(7) <ldms_sampler_base>`.

**config**\  **name**\ =<cfg_name> **plugin**\ =file_importer **dir_path**\ =<PATH>
**schema**\ =<schema-format> **instance**\ =<instance-format> **uid**\ =<user-id> **gid**\ =<group-id>
*<SAMPLER_BASE_OPTIONS>*


Description
-----------

   **name**\ *=<config_name>*
      The name of the configuration. If this is specified without
      **plugin** it assumed that **plugin** == **name**

   **plugin**\ *=file_importer*
      This must be *file_importer*

   **dir_path**\ *=<PATH>*
      The path to the directory from which all files will be imported.

   **sample_channel**\ *=<NAME>*
      The name of the LDMS Message channel to
      which the sampler will subscribe. Messages beginning the string
      "import" will cause the sampler to process all files in the
      configured *dir_path*. The default value is "file_importer_import".

   **publish**\ *[true|false]*
      If set to anything other than "true", message publication is
      disabled, and all `data_` options are ignored. Set data will not
      be published on the metric bus. The default is `false`.

   **data_host**\ *-<HOSTNAME>*
      The subscriber host name. The default is "localhost".

   **data_xprt**\ *-<XPRT>*
      The transport type for publishing data, e.g. 'sock', 'rdma', and 'fabric'.

   **data_channel**\ *=<NAME>*
      The name of the LDMS Message channel to which the imported file
      data will be published. The data is published to this channel at
      either the sample interval or when the "import" message is
      received on the *sample_channel*.

   **data_uid**\ *=<user-id>*
      The user-id that will be used to publish JSON messages to the data channel.
      Note that the **ldmsd** daemon must be owned by *root:root*
      to publish as anyone other than *geteuid()*

   **data_gid**\ *=<grouop-id>*
      The group-id that will be used to publish JSON messages to the data channel.
      Note that the **ldmsd** daemon must be owned by *root:root* to publish as
      anyone other than *getegid()*

   **data_perm**\ *=<octal-mask>*
      The permission bits placed in the published message data. See *open()* for detail
      on this value.

   **gid**\ *=<group-id>*
      The group-id that will be placed in the metadata of the metric set. This is only
      honored if the **ldmsd** is owned by root:root, otherwise it is the value returned
      by *getegid()*

   **uid**\ *=<user-id>*
      The user-id that will be placed in the metadata of the metric set. This is only
      honored if the **ldmsd** is owned by *root:root*, otherwise it is the value returned
      by *geteuid()*

   **gid**\ *=<group-id>*
      The group-id that will be placed in the metadata of the metric set. This is only
      honored if the **ldmsd** is owned by *root:root*, otherwise it is the value returned
      by *getegid()*

   **instance**\ *=<format-specifier>*
      The format specifier is a string that specifies how sets are named.
      There are two vectors in which the *instance name* should be unique.
      First, the *instance name* needs to be unique among other sets
      produced in the monitored system. This is to ensure that
      there are no name collisions among  other set names from different
      producers

      The format specifiers are as follows:

      - **%H** The value of the *HOSTNAME* environment variable
      - **%P** The value of the *producer* configuration attribute
      - **%p** The plugin name, i.e., 'file_importer'
      - **%C** The configuration name, i.e., the value specified by
	the *name* attribute in the **load** commmand
      - **%S** The value of the *schema* configuration attribute. The
	default value for the schema is: <plugin-name>'.'<config-name>'.'<basename(file-path)>'
      - **%U** The set owner user-id, or the value of the *uid* configuration parameter if the **ldmsd** is owned by *root*
      - **%G** The set owner group-id, or the value of the *gid* configuration parameter if the **ldmsd** is owned by *root*
      - **%F** The base name of the file from which data is being imported
      - **%J** The job_id from the specified *job_set* metric set

   For example:

   .. code-block:: text

      config name=fe2 instance="%H/%C/%U/%G/%p.%F"

   Produces something like: "calaluna/fe2/1000/1000/file_importer.smart2"

   - **schema**\ *=<format-specifier>*
      A format specifier that controls the naming of each set schema.
      The schema name *MUST* be unique within the sampler, but *SHOULD* be
      unique across the cluster for identically formatted sets to allow for
      reasonable storage and analysis.

    The format specifiers for **schema** are identical to those for **instance**.

   **<SAMPLER_BASE_OPTIONS>**
      Please see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for sampler_base options.

NOTES
=====

Files that contain parsing errors will result in the logging of error
messages. The file will be skipped and importing will continue with
the next file in *dir_path*.

FILES
=====

Files imported are self describing, i.e., the rules that govern how
the data is imported into a metric set is specified in the file
itself.

Overall the file consists of three main sections: the HELP section,
the TYPE section and the value section. The HELP section is formatted
as follows:

.. code-block:: text

   # HELP <metric-name> <help-string>

The HELP section specifies the *metric-name* to which the subsequent
TYPE and value sections apply. Although these sections repeat the
*metric-name*, only the name from the HELP section is used. The
*metric-name* from the remaining sections are assumed to match.

.. code-block:: text

   # TYPE <metric-name> <type> ldms={ <type-info> }

The *metric-name* must match the name from the HELP section. The
*type* is one of the following: "counter", "gauge", "histogram", or
definition, but is not otherwise considered.

The *type-info* encapsulated in the curly braces specifes important
information about how the metric value is imported into the metric set.
In particular, how it is stored in a list record if desired.

The *type-info* has the following syntax:

- **list**\ *=<list-name>*
- **record**\ *=<record-name>*
- **key_name**\ *=<key-name>*
- **key_type**\ *=<ldms-type-name>*
- **value_type**\ *=<ldms-type-name>*
- **list**\ *=<list-name>*

For example consider the following *type-info*
::

   # TYPE smartmon_unused_rsvd_blk_cnt_tot_threshold gauge ldms={list=disklist,record=smartmon,key_name="disk",key_type="char_array",value_type=s32,unit="count"}}

The *list-name* (disklist) specifies the metric name of the list
metric in the metric set. The metric list consists of one or more
record instances.

The *record-name* specifies the name of the record schema that will be
used to construct each record instance.

The *key-name* is the name of a metric in each record instance that is
use by the sampler to assign a value to a metric in a record
instance. Specifically as each value line is parsed, the *key-value*
from the *ldms* tag is used to search each record instance in the list
metric to determine which record instance contains this value. If the
record is found, the record instance is populated with the value. If
the record instance is **not** found, a new record is created and
added to the list.

The *key-type* is the type of the key metric specified by
*key-name*. The *value-type* is the type of the value metric. Both of
these values are one of: char, char_array, d64, f32, s16, s32, s64,
timestamp, u16, u32, u64, and u8.

The *unit* is a string that is used to tag the metric in the
record. This string is displayed next to the value in the **ldms_ls**
output.

In the following example:

.. code-block:: text

   # HELP smartmon_unused_rsvd_blk_cnt_tot_threshold SMART metric unused_rsvd_blk_cnt_tot_threshold
   # TYPE smartmon_unused_rsvd_blk_cnt_tot_threshold gauge ldms={list=disklist,record=smartmon,key_name="disk",key_type="char_array",value_type=s32,unit="count"}}
   smartmon_unused_rsvd_blk_cnt_tot_threshold{disk="/dev/sda",type="sat",smart_id="180"} 1
   smartmon_unused_rsvd_blk_cnt_tot_threshold{disk="/dev/sdb",type="sat",smart_id="180"} 1
   smartmon_unused_rsvd_blk_cnt_tot_threshold{disk="/dev/sdc",type="sat",smart_id="180"} 1

   The list is named "disklist", the record is named "smartmon", the
   record key is "disk", key type is "char_array", the value type is
   unsigned 32b integer and the unit string is "count".

   This results in **ldms_ls** output similar to the following:

   .. code-block:: text

      calaluna/fe/file_importer.fe.smart2: consistent, last update: Thu Aug 28 03:08:40 2025 -0400 [8257us]
      M u64          component_id                               0
      D u64          job_id                                     0
      D u64          app_id                                     0
      D list<>       disklist
      disk (Key) smartmon_unused_rsvd_blk_cnt_tot_threshold (count) smartmon_unused_rsvd_blk_cnt_tot_value (count) smartmon_unused_rsvd_blk_cnt_tot_worst (count)
      "/dev/sda"                                                  1                                            100                                            100
      "/dev/sdb"                                                  1                                            100                                            100
      "/dev/sdc"                                                  1                                            100                                            100 smartmon

MESSAGE CHANNELS
================

SAMPLE CHANNEL
--------------

The **file_importer** plugin supports sampling triggered by messages
published to an LDMS Message Channel. By default, this channel is
named *file_importer*, however, it can be changed with the
configuration option *sample_channel* as described above.

An application can trigger sampling of the configured directory by
writing the string "import" to the message channel as follows:

|
|  echo "import" | ldms_msg_publish -x sock -h localhost -p 10001 -m file_importer


Note that the **file_importer** sampler can also be configured to
scrub the configured directory periodically by starting the
sampler. The *start* command can appear either in a configuration file
or issued over the network with *ldmsd_controller* as follows:

|
| start name=file_importer interval=5s


DATA CHANNEL
============

When configured, the **file_importer** will translate the file data to
a JSON text object and publish this data on the configured LDMS message
channel. The *data_channel* configuration parameter specifies the
channel on which the data will be exported. The data for all files
will be transmitted on this same channel. Each JSON object is tagged
with the set name and other identifying information to allow for the
consuumer to distinguish between data sources.

Message Channel Data Format
---------------------------

This section describes the format of the JSON text object that encapsulates the imported  file data.





INPUT FILE EXAMPLE
==================

::

   # HELP smartmon_unused_rsvd_blk_cnt_tot_threshold SMART metric unused_rsvd_blk_cnt_tot_threshold
   # TYPE smartmon_unused_rsvd_blk_cnt_tot_threshold gauge ldms={list=disklist,record=smartmon,key_name="disk",key_type="char_array",metric_type=s32,unit="count"}}
   smartmon_unused_rsvd_blk_cnt_tot_threshold{disk="/dev/sda",type="sat",smart_id="180"} 1
   smartmon_unused_rsvd_blk_cnt_tot_threshold{disk="/dev/sdb",type="sat",smart_id="180"} 1
   smartmon_unused_rsvd_blk_cnt_tot_threshold{disk="/dev/sdc",type="sat",smart_id="180"} 1


BUGS
====

If running the sampler periodically with the *start* command, there is
no synchronization with an application writing to the configured
directory path.

If the application writing to the configured *dir_path* wishes to
achieve synchronization, it should use the *file_importer* LDMS
Message channel to do so. Specifically, write all file updates to
files in *dir_path* and then trigger a sample as described above.


SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`

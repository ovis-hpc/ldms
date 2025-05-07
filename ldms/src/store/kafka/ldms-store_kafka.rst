.. _store_kafka:

==================
store_kafka
==================

-----------------------------------------
Man page for the LDMS store_kafka plugin
-----------------------------------------

:Date:   2 Jun 2022
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller script:
| ldmsd_controller> load name=store_kafka
| ldmsd_controller> config name=store_kafka
  [path=<KAFKA_CONFIG_JSON_FILE>]
| ldmsd_controller> strgp_add name=<NAME> plugin=store_kafka
  container=<KAFKA_SERVER_LIST> decomposition=<DECOMP_CONFIG_JSON_FILE>

DESCRIPTION
===========

**store_kafka** uses librdkafka to send rows from the decomposition to
the Kafka servers (specified by strgp's *container* parameter) in JSON
format. The row JSON objects have the following format: { "column_name":
COLUMN_VALUE, ... }.

PLUGIN CONFIGURATION
====================

**config** **name=**\ *store_kafka* [ **path=\ KAFKA_CONFIG_JSON_FILE**
]

Configuration Options:

   **name=**\ *store_kafka*
      |
      | The name of the plugin. This must be **store_kafka**.

   **path=**\ *KAFKA_CONFIG_JSON_FILE*
      The optional KAFKA_CONFIG_JSON_FILE contains a dictionary with
      KEYS being Kafka configuration properties and VALUES being their
      corresponding values. **store_kafka** usually does not require
      this option. The properties in the KAFKA_CONFIG_JSON_FILE is
      applied to all Kafka connections from store_kafka. Please see
      `librdkafka CONFIGURATION
      page <https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md>`__
      for a list of supported properties.

STRGP CONFIGURATION
===================

**strgp_add** **name=**\ *NAME* **plugin=**\ store_kafka
**container=**\ *KAFKA_SERVER_LIST*
**decomposition=**\ *DECOMP_CONFIG_JSON_FILE*

strgp options:

   **name=**\ *NAME*
      |
      | The name of the strgp.

   **plugin=**\ store_kafka
      |
      | The plugin must be store_kafka.

   **container=**\ *KAFKA_SERVER_LIST*
      |
      | A comma-separated list of Kafka servers (host[:port]). For
        example: container=localhost,br1.kf:9898.

   **decomposition=**\ *DECOMP_CONFIG_JSON_FILE*
      |
      | Set-to-row decomposition configuration file (JSON format). See
        more about decomposition in :ref:`ldmsd_decomposition(7) <ldmsd_decomposition>`.

SEE ALSO
========

:ref:`ldmsd_decomposition(7) <ldmsd_decomposition>`

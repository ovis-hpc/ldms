.. _store_papi:

=================
store_papi
=================

-----------------------------------------
Man page for the LDMSD store_papi plugin
-----------------------------------------

:Date:   30 Sep 2019
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

Within ldmsd_controller or a configuration file:

**load** **name=store_papi**

**config** **name=store_papi** **path=**\ *STORE_ROOT_PATH*

**strgp_add** **name=**\ *STRGP_NAME* **plugin=store_papi**
**container=**\ *CONTAINER* **schema=**\ *SCHEMA*

**strgp_prdcr_add** **name=**\ *STRGP_NAME* **regex=**\ *PRDCR_REGEX*

DESCRIPTION
===========

**store_papi** is an LDMSD storage plugin for storing data from
**papi_sampler** specifically as it expects a collection of PAPI event
metrics after a certain job metric (task_ranks) that only
**papi_sampler** produced. **store_papi** stores data in a SOS container
(specified by **strgp** **container** option). Unlike **store_sos** (see
:ref:`store_sos(7) <store_sos>`) where an entire LDMS snapshot results in an
SOS data entry, **store_papi** split the PAPI events in the set into
their own schemas and data points. For example, if we have PAPI_TOT_INS
and PAPI_TOT_CYC as PAPI events in the **papi_sampler** set, we will
have PAPI_TOT_INS and PAPI_TOT_CYC schemas in the SOS container storing
respective PAPI events. This allows storing flexible, user-defined
schemas at run-time by user jobs (LDMS schemas of sets from
**papi_sampler** are defined at run-time by user jobs). Please note that
the schema name defined by user job must match **strgp**'s schema in
order to store the data.

CONFIG OPTIONS
==============

**name=store_papi**
   This MUST be store_papi (the name of the plugin).

**path=**\ *STORE_ROOT_PATH*
   The path to the root of the store. SOS container for each schema
   specified by the storage policy (**strgp**) will be placed in the
   *STORE_ROOT_PATH* directory.

STORAGE POLICY
==============

An LDMSD storage plugin is like a storage driver that provides only
storing mechanism. A storage policy (**strgp**) is a glue binding data
sets from various producers to a container of a storage plugin.

**strgp_add** command defines a new storage policy, identified by
**name**. The **plugin** attribute tells the storage policy which
storage plugin to work with. The **schema** attribute identifies LDMS
schema the data set of which is consumed by the storage policy. The
**container** attribute identifies a container inside the storage plugin
that will store data.

**strgp_prdcr_add** is a command to specify producers that feed data to
the storage policy.

BUGS
====

No known bugs.

EXAMPLES
========

Plugin configuration example:

   ::

      load name=store_papi
      config name=store_papi path=/var/store
      strgp_add name=papi_strgp plugin=store_papi container=papi schema=papi
      strgp_prdcr_add name=papi_strgp regex=.*

The following job script and PAPI JSON config combination is an example
of submiting a PAPI-enabled job that will end up in the storage of the
configuration above.

Job script example:

   ::

      #!/bin/bash
      export SUBSCRIBER_DATA='{"papi_sampler":{"file":"/tmp/papi.json"}}'
      srun bash -c 'for X in {1..60}; do echo $X; sleep 1; done'

PAPI JSON example (/tmp/papi.json):

   ::

      {
        "schema": "papi",
        "events": [
          "PAPI_TOT_INS",
          "PAPI_L1_DCM"
        ]
      }

SEE ALSO
========

:ref:`papi_sampler(7) <papi_sampler>`, :ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`,
:ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`.

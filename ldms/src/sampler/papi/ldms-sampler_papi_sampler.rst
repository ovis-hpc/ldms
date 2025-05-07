.. _papi_sampler:

===================
papi_sampler
===================

-------------------------------------------
Man page for the LDMSD papi_sampler plugin
-------------------------------------------

:Date:   30 Sep 2019
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

Within ldmsd_controller or a configuration file:
**config** **name=papi_sampler** **producer=**\ *PRODUCER*
**instance=**\ *INSTANCE* [ **component_id=\ COMP_ID** ] [
**stream=\ STREAM** ] [ **job_expiry=\ EXPIRY_SEC** ]

DESCRIPTION
===========

**papi_sampler** monitors PAPI events of processes of Slurm jobs.

The job script must define **SUBSCRIBER_DATA** environment variable as a
JSON object that has at least **"papi_sampler"** attribute as follows:

   ::

      SUBSCRIBER_DATA='{"papi_sampler":{"file":"/PATH/TO/PAPI.JSON"}}'

where the **"file"** attribute inside **"papi_sampler"** points to a
JSON-formatted text file containing user-defined schema name and PAPI
events of interest, e.g.

   ::

      {
        "schema": "my_papi",
        "events": [
          "PAPI_TOT_INS",
          "PAPI_L1_DCM"
        ]
      }

**papi_sampler** relies on **slurm_notfifier** SPANK plugin to notify it
about the starting/stopping of jobs on the node over ldmsd_stream.
Please consult **:ref:`slurm_notifier(7) <slurm_notifier>`** for more information on how
to deploy and configure it. The value of SUBSCRIBER_DATA from the job
script is carried over to **papi_sampler** when the job started, and an
LDMS set will be created according to the PAPI JSON file pointed by the
SUBSCRIBER_DATA. In the case of multi-tenant (multiple jobs running on a
node), each job has its own set. The set is deleted after *job_expiry*
period after the job exited.

CONFIG OPTIONS
==============

**name=papi_sampler**
   This MUST be papi_sampler (the name of the plugin).

**producer=**\ *PRODUCER*
   The name of the data producer (e.g. hostname).

**instance=**\ *INSTANCE*
   This is mandatory due to the fact that **papi_sampler** extends
   **sampler_base** and this option is required by **sampler_base**
   config. However, the value is ignored and can be anything. The actual
   name of the **papi_sampler** instance is
   *PRODUCER*/*SCHEMA*/*JOB_ID*.

**component_id=**\ *COMPONENT_ID*
   An integer identifying the component (default: *0*).

**stream=**\ *STREAM*
   The name of the stream that **slurm_notifier** SPANK plugin uses to
   notify the job events. This attribute is optional with the default
   being *slurm*.

**job_expiry=**\ *EXPIRY_SEC*
   The number of seconds to retain the set after the job has exited. The
   default value is *60*.

BUGS
====

No known bugs.

EXAMPLES
========

Plugin configuration example:

   ::

      load name=papi_sampler
      config name=papi_sampler producer=node0 instance=NA component_id=2 job_expiry=10
      start name=papi_sampler interval=1000000 offset=0

Job script example:

   ::

      #!/bin/bash
      export SUBSCRIBER_DATA='{"papi_sampler":{"file":"/tmp/papi.json"}}'
      srun bash -c 'for X in {1..60}; do echo $X; sleep 1; done'

PAPI JSON example:

   ::

      {
        "schema": "my_papi",
        "events": [
          "PAPI_TOT_INS",
          "PAPI_L1_DCM"
        ]
      }

SEE ALSO
========

:ref:`slurm_notifier(7) <slurm_notifier>`, :ref:`syspapi_sampler(7) <syspapi_sampler>`,
:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldms_sampler_base(7) <ldms_sampler_base>`.

.. _shm_sampler:

===========
shm_sampler
===========

------------------------------------------------------------------------------------------------------
This is a sampler plug-in module within the the LDMS that can read from a dynamic number of shm files.
------------------------------------------------------------------------------------------------------

:Date:   5 March 2018
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| load name=shm_sampler
| config name=shm_sampler [ <attr>=<value> ]

DESCRIPTION
===========
**shm_sampler** is a sampler plug-in module within the the LDMS. This sampler can read
from a dynamic number of shm files. These files are tracked by a central
index file in shared memory. The main usage of this sampler is to stream
application performance data.

| Configuration options:
| producer=<name> instance=<name>
  [shm_index=<name>][shm_boxmax=<int>][shm_array_max=<int>][shm_metric_max=<int>]
  [shm_set_timeout=<int>][component_id=<int>] [schema=<name>]
  [job_set=<name> job_id=<name> app_id=<name> job_start=<name>
  job_end=<name>]

**producer**
  A unique name for the host providing the data

**instance**
  A unique name for the metric set

**shm_index**
  A unique name for the shared memory index file

**shm_boxmax**
  Maximum number of entries in the shared memory index file

**shm_array_max**
  Maximum number of elements in array metrics

**shm_metric_max**
  Maximum number of metrics

**shm_set_timeout**
  No read/write timeout in seconds

**component_id**
  A unique number for the component being monitored, Defaults to zero.

**schema**
  The name of the metric set schema, Defaults to the sampler name

**job_set**
  The instance name of the set containing the job data, default is 'job_info'

**job_id**
  The name of the metric containing the Job Id, default is 'job_id'

**app_id**
  The name of the metric containing the Application Id, default is 'app_id'

**job_start**
  The name of the metric containing the Job start time, default is 'job_start'

**job_end**
  The name of the metric containing the Job end time, default is 'job_end'

BUGS
====

None known.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=shm_sampler
   config name=shm_sampler producer=samplerd instance=samplerd/shm_sampler shm_index=/ldms_shm_mpi_index shm_boxmax=4 component_id=23
   start name=shm_sampler interval=1000000 offset=0

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`

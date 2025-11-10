.. _ldmsd_multi_tenant:

==================
ldmsd_multi_tenant
==================

-------------------------------------
Manual for LDMSD Multi-Tenant Feature
-------------------------------------

:Date: September 2025
:Manual section: 7
:Manual group: LDMSD

SYNOPSIS
========

tenant_def_add
   name=\ *NAME* metrics=\ *METRICS*

tenant_def_del
   name=\ *NAME*

DESCRIPTION
===========

The multi-tenant feature enables users to track and monitor multiple concurrent
tenants (such as jobs, users, or tasks) within LDMS metric sets. Users can
define custom tenant definitions specifying which attributes to track, then
associate those definitions with LDMS sets to automatically collect
tenant-specific data alongside the set metrics.

The multi-tenant system integrates with the LDMS job manager interface to query
tenant attributes dynamically. Each time a sampler collects metrics, it also
updates the list of active tenants and their attribute values.

Key Concepts
------------

**Tenant Definition**
  A named template that specifies which attributes identify a tenant. Examples:

  - A job-level tenant: ``job_id``
  - A job-step tenant: ``job_id, step_id``
  - A task-level tenant: ``job_id, step_id, task_id``

**Tenant Attributes**
  Individual properties that describe a tenant, such as ``job_id``, ``user_id``,
  ``step_id``, or ``task_id``. These must be valid metrics recognized by the
  job manager.

**Tenant Instance**
  A specific combination of attribute values representing one tenant. For example,
  with a definition of ``job_id, step_id``, the instance ``{job_id=12345, step_id=2}``
  represents one tenant.

**Tenant List**
  A list metric within an LDMS set containing all currently active tenant instances.
  This list is updated each time the sampler runs.

Workflow
--------

1. **Define tenant types** using ``tenant_def_add`` to specify which attributes
   constitute a tenant

2. **Configure samplers** to use a tenant definition by specifying the ``tenant``
   parameter

3. **Start sampling** - the sampler will automatically query and update tenant
   information on each sample interval

CONFIGURATION
=============

Tenant Definition Command
--------------------------

**tenant_def_add** creates a new tenant definition.

   name=NAME
      Unique identifier for this tenant definition. This name is referenced when
      configuring samplers.

   metrics=METRICS
      Comma-separated list of attribute names that define a tenant. Each attribute
      must be a valid metric available from the job manager. The order of attributes
      matters - two tenants are considered different if they differ in any attribute
      value.

**tenant_def_del** deletes a tenant definition

   name=NAME
      The name of the tenant definition to be deleted

**Example:**

.. code-block:: bash

   # Define a tenant by job, step, and task
   tenant_def_add name=job_step_task_tenant metrics=job_id,step_id,task_id

   # Define a tenant by job only
   tenant_def_add name=job_tenant metrics=job_id

   # Define a tenant by job and user
   tenant_def_add name=job_user_tenant metrics=job_id,user_id

Sampler Configuration
---------------------

To enable tenant tracking in a sampler, add the ``tenant`` parameter to the
sampler's ``config`` command.

**Parameter:**

``tenant=<TENANT_DEF_NAME>``
  Name of the tenant definition to use (created with ``tenant_def_add``).
  Optional - if omitted, no tenant data is collected.

**Example:**

.. code-block:: bash

   load name=meminfo
   config name=meminfo producer=nid0001 instance=nid0001/meminfo tenant=job_step_task_tenant
   start name=meminfo interval=1s

EXAMPLES
========

Basic Configuration
-------------------

Track jobs with their steps and tasks:

.. code-block:: bash

   # Define tenant structure
   tenant_def_add name=job_step_task_tenant metrics=job_id,step_id,task_id

   # Configure sampler to use it
   load name=meminfo
   config name=meminfo producer=node001 instance=node001/meminfo \
          tenant=job_step_task_tenant
   start name=meminfo interval=1s

Multiple Tenant Definitions
----------------------------

Different samplers can use different tenant definitions:

.. code-block:: bash

   # Define multiple tenant types
   tenant_def_add name=job_tenant metrics=job_id
   tenant_def_add name=job_step_tenant metrics=job_id,step_id

   # Use job-level tracking for system metrics
   load name=meminfo
   config name=meminfo producer=node001 instance=node001/meminfo \
          tenant=job_tenant
   start name=meminfo interval=1s

   # Use job-step tracking for application metrics
   load name=app_sampler
   config name=app_sampler producer=node001 instance=node001/app \
          tenant=job_step_tenant
   start name=app_sampler interval=5s

Complete Configuration File
----------------------------

.. code-block:: bash

   # samplerd.conf

   # Define what a tenant is for this system
   tenant_def_add name=job_step_task_tenant metrics=job_id,step_id,task_id

   # Load and configure sampler plugins
   load name=meminfo
   config name=meminfo producer=nid0001 instance=nid0001/meminfo \
          tenant=job_step_task_tenant
   start name=meminfo interval=1s

   load name=vmstat
   config name=vmstat producer=nid0001 instance=nid0001/vmstat \
          tenant=job_step_task_tenant
   start name=vmstat interval=1s

DATA STRUCTURE
==============

When a sampler is configured with a tenant definition, its LDMS set includes:

**tenant_def**
  Record definition describing the structure of tenant records (includes all
  attributes specified in the tenant definition)

**tenants**
  List of active tenant instances, where each element is a record containing
  the values of all tenant attributes

The tenant list is updated each time the sampler runs. Tenants that have
terminated are automatically removed, and new tenants are added.

NOTES
=====

- Tenant definitions must be created before configuring samplers that use them

- The same tenant definition can be reused across multiple samplers

- Tenant attributes must be valid metrics provided by the configured job manager

- If more tenants exist than initially estimated, the LDMS set may need to be
  resized automatically (this is handled internally)

- If no tenants are active at sampling time, the tenant list will be empty

- Tenant tracking has minimal performance overhead - only a job manager query
  per sample interval

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`

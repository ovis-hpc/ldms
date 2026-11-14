==========================
ldms_amdgpu_schema_name
==========================

:Date: 2026-11-14
:Version: OVIS-4
:Manual section: 1
:Manual group: LDMS Tools

SYNOPSIS
=========

**ldms_amdgpu_schema_name** [ <attr>=<value> ... ]

DESCRIPTION
============

The **ldms_amdgpu_schema_name** utility computes the schema name that will be used by
the amdgpu_sampler plugin for a given configuration. This is useful for configuring
storage plugins that need to know the exact schema name in advance.

When use_schema_hash is enabled, the amdgpu_sampler generates unique schema names by
appending a hash suffix based on the selected fields. This utility allows you to
precompute that schema name.

OPTIONS
========

**schema** =<name>
   |
   | Base schema name. Default: "amdgpu_sampler"

**fields** =<list>
   |
   | Comma-separated field names or alias. Default: base set of common fields
   | See ldms-sampler_amdgpu_sampler(7) for available fields and aliases.

**use_schema_hash** =<0|1>
   |
   | If set to 1, append a hash suffix to the schema name. Default: 0 (no hash)

EXAMPLES
=========

Compute schema name with default configuration (no hash)::

  $ ldms_amdgpu_schema_name
  amdgpu_sampler

Compute schema name with hash enabled::

  $ ldms_amdgpu_schema_name use_schema_hash=1
  amdgpu_sampler_a1b2c3d4

Compute schema name for specific fields with hash::

  $ ldms_amdgpu_schema_name fields=temperature_hotspot,power_socket,clock_gfxclk use_schema_hash=1
  amdgpu_sampler_5e6f7890

Compute schema name with field alias (no hash)::

  $ ldms_amdgpu_schema_name fields=all_temps
  amdgpu_sampler

Compute schema name with custom base name and hash::

  $ ldms_amdgpu_schema_name schema=mygpu fields=all_power use_schema_hash=1
  mygpu_1234abcd

USAGE IN CONFIGURATION
=======================

The computed schema name can be used when configuring storage plugins. For example,
to store amdgpu_sampler data with specific fields using unique schemas::

  # Compute the schema name with hash
  SCHEMA=$(ldms_amdgpu_schema_name fields=temperature_hotspot,power_socket,utilization_gfx use_schema_hash=1)

  # Configure storage plugin
  load name=store_csv
  config name=store_csv path=/data schema=${SCHEMA}

  # Configure sampler with matching fields and hash enabled
  load name=amdgpu_sampler
  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi \\
         component_id=1 fields=temperature_hotspot,power_socket,utilization_gfx use_schema_hash=1
  start name=amdgpu_sampler interval=1000000

Without hash suffix (default)::

  # Simple schema name
  SCHEMA=$(ldms_amdgpu_schema_name)

  # Configure storage plugin
  load name=store_csv
  config name=store_csv path=/data schema=${SCHEMA}

  # Configure sampler (no use_schema_hash needed)
  load name=amdgpu_sampler
  config name=amdgpu_sampler producer=node1 instance=node1/amdsmi component_id=1
  start name=amdgpu_sampler interval=1000000

SEE ALSO
=========

ldms-sampler_amdgpu_sampler(7), ldmsd(8), ldms_quickstart(7)

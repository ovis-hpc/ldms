.. _store_test:

================
store_test
================

---------------------------------------
Man page for the LDMS store_test plugin
---------------------------------------

:Date:   04 Aug 2026
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| load name=NAME plugin=store_test

DESCRIPTION
===========

The `store_test` plugin emulates storage latency without persisting any
data. Each `store()` or `commit()` call sleeps for a configured duration
and returns; no data is written to memory, a file, or any backend.

**store_test does not store data.** It exists to support performance and
throughput testing of the *ldmsd* storage pipeline (worker pool, queueing,
backpressure) where a deterministic, controllable store time is required
and a real backend's I/O variance would confound results. Do not use
`store_test` in a production storage policy where data retention is
expected. For correctness or data-persistence testing, use a real store
plugin such as `store_csv` or `store_sos` instead.

To build the store_test plugin, specify the **--enable-ldms-test** flag
when running configure.

This plugin is multi-instance capable.

STORE_TEST INIT CONFIGURATION ATTRIBUTE SYNTAX
================================================

**config**
   | name=NAME latency=TIME

   name=NAME
      | This is the storage plugin instance name.

   latency=TIME
      | The time that `store()` and `commit()` sleep before returning.
      | No data is persisted regardless of this value. TIME accepts a
      | number with an optional unit suffix: `us` (microseconds, the
      | default unit if none is given), `ms` (milliseconds), `s`
      | (seconds), `m` (minutes), `h` (hours), or `d` (days) -- e.g.
      | ``500us``, ``10ms``, ``1s``, ``2m``. A value of ``0`` or
      | a negative number means no delay. This delay is fixed and
      | applies once per `store()`/`commit()` call; it does not scale
      | with the number of rows in a decomposed commit.

BUGS
====

No known bugs.

EXAMPLES
========

Simple store_test Configuration (legacy store)
------------------------------------------------

This configures `store_test` with a fixed 500-microsecond delay for a
storage policy using the legacy `store()` path (`schema=` rather than
`regex=`). The example below shows the relevant lines in the **ldmsd**
configuration file.

::

   load name=ts1 plugin=store_test
   config name=ts1 latency=500
   strgp_add name=strgp_test plugin=ts1 schema=meminfo
   strgp_start name=strgp_test

Decomposed store_test Configuration
-------------------------------------

This configures `store_test` for a storage policy using the decomposed
`commit()` path, with a decomposition configuration file identifying the
rows to be produced from each matching schema.

::

   load name=ts1 plugin=store_test
   config name=ts1 latency=200us
   strgp_add name=strgp_test plugin=ts1 \
      decomposition=/opt/ovis/etc/decomp.json regex=.*
   strgp_prdcr_add name=strgp_test regex=.*
   strgp_start name=strgp_test

Using store_test as a Latency Baseline
------------------------------------------

Because `store_test`'s store time is fixed and does not depend on real
backend I/O, it can be used alongside a real store plugin (e.g.
`store_sos`) to separate storage-pipeline overhead from backend-specific
I/O variance when interpreting `store_time_stats` output.

::

   load name=baseline plugin=store_test
   config name=baseline latency=1ms
   strgp_add name=strgp_baseline plugin=baseline \
      decomposition=/opt/ovis/etc/decomp.json regex=.*
   strgp_prdcr_add name=strgp_baseline regex=.*
   strgp_start name=strgp_baseline

   load name=real plugin=store_sos
   config name=real path=/var/tmp
   strgp_add name=strgp_real plugin=real \
      decomposition=/opt/ovis/etc/decomp.json regex=.*
   strgp_prdcr_add name=strgp_real regex=.*
   strgp_start name=strgp_real

Comparing `strgp_baseline`'s reported commit time against the
configured 1000 us shows storage-pipeline overhead in isolation.
Comparing `strgp_real`'s commit time against `strgp_baseline`'s then
isolates the backend's own contribution to store latency::

  ldmsd_controller -a munge -p 10002
  Welcome to the LDMSD control processor
  sock:localhost:10002> store_time_stats

SEE ALSO
========

   :ref:`ldmsd(7) <ldmsd>`
   :ref:`ldmsd_decomposition(7) <ldmsd_decomposition>`
   :ref:`store_csv(7) <store_csv>`
   :ref:`store_sos(7) <store_sos>`

.. _perfevent2:

==========
perfevent2
==========

----------------------------------------------
ldmsd plugin for perf events (core and uncore)
----------------------------------------------

:Date: 5 Jan 2026
:Manual section: 7
:Manual group: LDMS sampler

SYNOPSIS
========

``ldmsd``'s ``config`` commands:

.. parsed-literal::

   ``load`` ``name``\ =\ *PLUG_INST_NAME* ``plugin``\ =\ **perfevent2**

   ``config`` ``name``\ =\ *PLUG_INST_NAME* ``producer``\ =\ *PRODUCER*
          ``instance``\ =\ *INSTANCE* ``conf``\ =\ *PERF_CONF_JSON*
          [``perfdb``\ =\ *PERFDB_JSON*]

   ``start`` ``name``\ =\ *PLUG_INST_NAME* ``interval``\ =\ *INTERVAL*


Auxillary perfdb generator script:

.. parsed-literal::

   ``ldms-perfdb-gen`` [``-L`` *LINUX_SRC*] [``-a`` *ARCH*] [``-o`` *OUTPUT*]

List supported events:

.. parsed-literal::

   ``perf`` list hw cache pmu

DESCRIPTION
===========

perfevent2 plugin sets up Linux performance event counters according to
the given *PERF_CONF_JSON* file (see `PERF_CONF_JSON_FILE`_ section
for more info). The counters are read every given *INTERVAL*.

perfevent2 supports multiple plugin instances. The following list explains each
of the attributes:

- ``name`` attribute given to ``load`` command is the plugin instance name.

- ``plugin`` attribute (of ``load`` command) must be ``perfevent2``, indicating
  the plugin we want to load.

- ``instance`` attribute of ``config`` command refers to LDMS set name to be
  created by the plugin instance.

- ``conf`` attribute of ``config`` command is a path to JSON file containing
  perf event configurations (e.g. what events to probe, on what CPU). See
  `PERF_CONF_JSON_FILE`_ section for more info.

- ``perfdb`` (optional) attribute of ``config`` command is a path to perf event
  database (in JSON format) created from running ``ldms-perfdb-gen`` command.

In order to support PMU events (vendor-specific events), perfevent2 plugin
requires ``perfdb`` event database. The events are defined in JSON files in the
Linux kernel source tree and the Linux ``perf`` utility baked the PMU database
into its binary. ``ldms-perfdb-gen`` is the script that aggregates the events
from the Linux kernel source tree and reformatted it into *PERFDB_JSON* suitable
for perfevent2 to consume. We opt for generating the database instead of baking
the events into the plugin because when the kernel (and perf) is updated, we
would renerate the database instead of recompiling the plugin. Here are a few
examples:

.. code:: sh

   # Generate from a local Linux source
   $ ldms-perfdb-gen -L /PATH/TO/LINUX/SRC -o ldms-perfdb.json

   # Download matching tarball from kernel.org and generate perfdb from it
   $ ldms-perfdb-gen -o ldms-perfdb.json

For more information, please see ``"ldms-perfdb-gen --help"``.


SET FORMAT
----------

The set produced by perfevent2 has the following format:

.. code:: python

   # Borrowing Python dict syntax to describe the set
   {
     "component_id": _INT_, # from base_sampler
     "job_id": _INT_, # from base_sampler
     "app_id": _INT_, # from base sampler
     "counter_recdef": <__record_type__>, # record definition for long counters
     "scaled_counter_recdef": <__record_type__>, # record definition for scaled counters (double)
     "counters": <_list_>, # list of counter records
     "sacled_counters": <_list_>, # list of sacled counter records
   }

   # each counter record can be described as follows
   {
     "name": _STR_, # counter name e.g. "instructions"
     "pid": _INT_,  # the PID (-1 for system-wide)
     "counters": _u64_array_, # array of u64 event counter for each CPU
   }

   # each sacled_counter record can be described as follows
   {
     "name": _STR_, # counter name e.g. "instructions"
     "pid": _INT_,  # the PID (-1 for system-wide)
     "scaled_counters": _double_array_, # array of double-precision scaled
                                        # counter for each CPU
   }


PERF_CONF_JSON FILE
===================

.. _PERF_CONF_JSON_FILE:


The *PERF_CONF_JSON* file format is described as follows:

.. code:: json

  {
    "events": [
      {
        "event": "EVENT_NAME",
        "cpu":   "RANGES" /* optional */
      },
      ...
    ],
    "perfdb": "/PATH/TO/PERFDB" /* optional */
  }

The name of the ``event`` is the same name used in ``perf`` program. To see a
list of supported events, run ``perf list hw cache pmu``, which lists hardware,
cache, and vendor-specific events. The other kinds of events (e.g.
``software``) are not supported by perfevent2 plugin.

The optional ``cpu`` attribute in each event is a string that contain ranges of
numbers specifying the CPUs to get the event. For example, "cpu": "0-3,8-10"
limits the events to just CPU0, CPU1, CPU2, CPU3, CPU8, CPU9, and CPU10. Uncore
events (e.g. from ``amd_l3`` PMU) usually applied to certain CPUs. For example,
``l3_lookup_state.l3_miss`` (``amd_l3`` PMU) on AMD Ryzen 9 7950X3D 16 core
processor only applies to "cpu":"0,8" (representing 8 cores on one die and other
8 cores on another die). When ``cpu`` is not specified, all *applicable* CPUs
are included.

The ``perfdb`` attribute (at the top level) is an optional attribute containing
the path to the *PERFDB_JSON* file. Users have an option to specify ``perfdb``
here or at the ``config`` command (``config`` command has more precedence).


NOTES
=====

The official way of knowing if ``perf_event_open()`` support is enabled is
checking for the existence of the file /proc/sys/kernel/perf_event_paranoid.

The non-root ``ldmsd`` also requires ``CAP_PERFMON`` to use Linux perf
(see ``capabilities``\ (7) man page).


BUGS
====

No known bugs.


EXAMPLES
========

Example1
--------

In this example:

- ``perfdb`` location is specified in ``perfconf.json``

- only ``event`` attribute is specified in the events config list, which
  instruct ``perfevent2`` plugin to count the events on all applicable CPUs.

``ldms.conf``

.. code::

   load name=p0 plugin=perfevent2
   config name=p0 producer=node1 instance=node1/perf conf=/etc/ldms/perfconf.json
   start name=p0 interval=1s


``/etc/ldms/perfconf.json``

.. code:: json

   {
     "events": [
       { "event": "power/energy-pkg/", },
       { "event": "instructions" },
       { "event": "cpu/L1-dcache-loads/" }
       { "event": "l3_lookup_state.l3_miss" }
     ],
     "perfdb": "/opt/ovis/lib/ovis-ldms/ldms-perfdb.json"
   }


Example2
--------

This example yields the same result as Example1, but with ``cpu`` being
explicitly specified, and ``perfdb`` being specified in the ``config`` line
instead.

``ldms.conf``

.. code::

   load name=p0 plugin=perfevent2
   config name=p0 producer=node1 instance=node1/perf \
                  conf=/etc/ldms/perfconf.json \
                  perfdb=/opt/ovis/lib/ovis-ldms/ldms-perfdb.json
   start name=p0 interval=1s


``/etc/ldms/perfconf.json``

.. code:: json

   {
     "events": [
       { "event": "power/energy-pkg/", "cpu": 0 },
       { "event": "instructions", "cpu": "0-31" },
       { "event": "cpu/L1-dcache-loads/", "cpu": "0-31" }
       { "event": "l3_lookup_state.l3_miss", "cpu": "0,8" }
     ]
   }


REMARK: It is OK to include inapplicable CPUs to the events. ``perfevent2``
plugin will masked off the inapplicable CPUs from the specified ranges. For
example, specifying ``"cpu":"0-16"`` to ``l3_lookup_state.l3_miss`` event will
result in only CPU0 and CPU8 being selected (according to ``cpumask`` in
``/sys`` for this PMU).


SEE ALSO
========

``perf_event_open``\ (2),
:ref:`ldmsd(7) <ldmsd>`,
:ref:`ldms_quickstart(7) <ldms_quickstart>`,
:ref:`ldms_sampler_base(7) <ldms_sampler_base>`

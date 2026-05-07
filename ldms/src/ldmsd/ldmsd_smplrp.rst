.. _ldmsd_smplrp:

============
ldmsd_smplrp
============

--------------------
LDMSD Sampler Policy
--------------------

:Date: 2026-04-07
:Manual section: 7
:Manual group: LDMS
:Version: LDMS 4.5

SYNOPSIS
========

.. parsed-literal::
  ``smplrp_add`` ``name``\ =\ `NAME` ``path``\ =\ `SMPLRP_JSON_PATH`

  ``smplrp_start`` ``name``\ =\ `NAME`

  ``smplrp_stop`` ``name``\ =\ `NAME`

  ``smplrp_del`` ``name``\ =\ `NAME`


DESCRIPTION
===========

An LDMSD Sampler Policy (``smplrp``) is a configuration object in LMDSD that
manipulates sampler plugin instances according to the configuration provided in
`SMPLRP_JSON_PATH`. Currently, ``smplrp`` supports policies with actions based
on Slurm ``job_init`` and ``job_exit`` events:

 - load/term action:

   - ``load`` + ``config`` + ``start`` a new plugin instance on ``job_init`` event

   - ``term`` the plugin instance (with delay) on ``job_exit`` event

 - interval modification action:

   - ``stop`` + ``start`` a plugin instance with the specified interval on
     ``job_init`` and/or ``job_exit`` events.

 The JSON configuration format for ``smplrp`` is described in the following
 section.



``smplrp`` JSON Configuration
=============================

``smplrp`` JSON file expects to have a dictionary as a root object as follows:

.. code:: python

   {
     "message_tag": "__STR__", # optional
     "component_id": "__COMPONENT_ID__",
     "job_event_actions": [
       {
         # action_1; see ACTIONS section below
       },
       {
         # action_2; see ACTIONS section below
       },
       ...
     ]
   }

``message_tag`` is an optional option informing the policy about which
``ldms_msg`` tag it should subscribe to receive ``slurm`` job information from
the ``slurm_notifier``. The default value is "slurm".

``component_id`` value is used to supply ``component_id=VALUE`` parameter of the
``config`` command. This is required by most of the sampler plugins. The
specified `VALUE` can be an environment variable, e.g.

      "component_id": "${COMP_ID}"

``job_event_actions`` is a `LIST` of action objects. The actions specified in
this list are executed on ``job_init`` and ``job_exit`` events.


ACTIONS
=======

There are currently two kinds of actions: ``load_term`` and ``interval_mod``.

A ``load_teerm`` action ``load`` + ``config`` + ``start`` a sampler plugin
instance on ``job_init`` event, and ``term`` the plugin instance (with delay) on
``job_exit`` event.

An ``interval_mod`` action modifies the sampling interval of the target plugin
instance on ``job_init`` and ``job_exit`` events. For example, one might want to
sample 10Hz when there is a (or multiple) job running, and goes back to 1Hz when
there are no jobs running.

Please see `LOAD_TERM ACTION`_ section and `INTERVAL_MOD ACTION`_ section for
for their respective description and configuration.


LOAD_TERM ACTION
================

``load_term`` action acts on ``job_init`` and ``job_exit`` events.

On ``job_init`` event, the ``load_term`` action does the following:

1. Issues \`\ ``load name=``\ `NAME` ``plugin=``\ `PLUGIN`\` command. The value
   of `NAME` and `PLUGIN` are determined from ``load_term["name"]`` and
   ``load_term["plugin"]`` respectively. The ``load_term["name"]`` is treated as
   a STRING TEMPLATE (see STRING TEMPLATE section) where "%J" is substributed by
   Slurm Job ID. To avoid plugin name collision, it is advisable to include "%J"
   as a part of the ``load_term["name"]``.

2. Issues \`\ ``config name=``\ `NAME` ``producer=``\ `PRDCR` ``instance=``\ `INST`
   ``...``\` command (or commands) to configure the plugin instance from 1) if
   ``load`` succeeded. The `NAME`, `PRDCR` and `INST` values are determined
   from ``load_term["name"]``, ``load_term["producer"]`` and
   ``load_term["instance"]`` (STRING TEMPLATE) respectively. The ``...`` extra
   config command attributes are obtained from attribute-values in
   ``load_term["config"]`` object.

   In the case that the sampler plugin requires multiple ``config`` commands to
   work, ``load_term["configs"]`` (notice the 's') `LIST` of config object can
   be specified.

   REMARK: The values may contain environment variables (e.g. ``${HOSTNAME}``),
   and they will be used to construct ``config`` command(s). The config handler
   logic in ``ldmsd_requesrt`` will replace the values accordingly like a config
   command from configuration file.

3. Issues \`\ ``start name=``\ `NAME` ``interval=``\ `INTERVAL` ``offset=``\
   `OFFSET`\` command after successful config command(s). The `NAME`,
   `INTERVAL` and `OFFSET` are obtained from ``load_term["name"]``,
   ``load_term["interval"]`` and ``load_term["offset"]`` respectively.


On ``job_exit`` event, the ``load_term`` action schedules a ``timeout`` event
specified by ``load_term["term_delay"]`` that will later issue \`\ ``term
name=``\ `NAME`\` command. If ``load_term["term_delay"]`` is not specified, the
``term`` command is issued immediately on ``job_exit`` event.

The ``load_term`` action configuration format and attributes are described as
follows:

.. code:: python

  {
    "type": "load_term",

    "plugin": "__STR__",    # [required] plugin name e.g. "meminfo"

    "name": "__TEMPLATE__", # [optional] plugin instance name e.g. "foo-%J", see STRING TEMPLATE section
                            # default: "%N-%L-%J"

    "producer": "__STR__",  # [optional] e.g. "${HOSTNAME}"
                            # default: gethostname()

    "instance": "__TEMPLATE__", # [optional] LDMS set name e.g. "${HOSTNAME}/foo/%J"
                                # default: "%P/%N-%L-%J"

    "interval": "__TIME_STR__", # [optional] sampling interval e.g. "1s"
                                # default: "1s"

    "offset": "__TIME_STR__",   # [optional] sampling e.g. "200ms"
                                # default: "0s"

    "term_delay": "__TIME_STR__", # [optional] plugin termination delay e.g. "10s"
                                  # default: "0s"
    "config": {  # [optional]
      # additional attribute-values for `config` command
      "__ATTR0__": "__VALUE0__",
      ...
      # The non-attribute-value config arguments (keywords) can be specifid
      # with attribute with null value.
      "__KEYWORD0__": null,
      ...
    },
    "configs": [
      # [optional] Each object in this list will result in a config call. This
      #            is to support plugins that require multiple config calls.
      {
        # config_0
        "__ATTR_0_0__": "__VALUE_0_0__",
        ...
        "__KEYWORD_0_0__": null,
        ...
      },
      {
        # config_1
        "__ATTR_1_0__": "__VALUE_1_0__",
        ...
        "__KEYWORD_1_0__": null,
        ...
      },
      ...
    ]
  }


INTERVAL_MOD ACTION
===================

``interval_mod`` action (``imod`` for short) acts on ``job_init`` and
``job_exit`` events.

On ``job_init`` event of the FIRST tenant, the ``imod`` action performs stop +
start with the new interval from ``imod["job_init"]`` to the targeted plugin.
If the job is not the first tenant (there are other jobs started before it and
still running), ``imod`` ignores the ``job_init`` event.

On ``job_exit`` event of the LAST tenant, the ``imod`` action performs stop +
start with the new interval from ``imod["job_exit"]`` to the targeted plugin.
If the job is not the last tenant (there are other jobs still running), ``imod``
ignores the ``job_exit`` event.

.. code:: python

  {
    "type": "interval_mod",
    "name": "__TEMPLATE__", # [required] identifier of the plugin to modify.
                            # Also see STRING TEMPLATE section.

    "job_init": { # [required] The sampling interval for `job_init` event
      "interval": "__TIME_STR__", # [required]
      "offset": "__TIME_STR__"    # [optional] default "0s"
    },
    "job_exit": { # [required] The sampling interval for `job_exit` event
      "interval": "__TIME_STR__", # [required]
      "offset": "__TIME_STR__"    # [optional] default "0s"
    }
  }


STRING TEMPLATE
===============

Some configuration values (e.g. ``load_term["name"]``) supports string template
expansion. The special ``%<CHAR>`` placeholders will be replaced as follows:

- ``%J`` : Slurm Job ID.
- ``%C`` : ``smplrp["component_id"]``.
- ``%P`` : ``load_term["producer"]``.
- ``%L`` : ``load_term["plugin"]``.
- ``%N`` : the policy name (from ``smplrp_add`` command).
- ``%W`` : the ``WORKFLOW_ID`` (from job env).
- ``%%`` : literal ``%``.


Example 1
=========

This example shows minimal configuration for both ``load_term`` action and
``interval_mod`` action. Attributes that have default values are omitted.

``ldmsd.conf`` file:

.. code:: aconf

  msg_enable
  smplrp_add name=sp0 path=sp0.json
  smplrp_start name=sp0



``sp0.json`` file:

.. code:: python

  {
    "component_id": "${COMPONENT_ID}",
    "job_event_actions": [
      {
        "type": "load_term",
        "plugin": "procnetdev2",
      },
      {
        "type": "interval_mod",
        "name": "meminfo",
        "job_init": { "interval": "0.1s" },
        "job_exit": { "interval": "1s"   }
      }
    ]
  }


To concretely explain what will happen on ``job_init`` and ``job_exit`` event,
let's suppose that:

  - ``ldmsd`` is running on ``node020``.

  - ``${COMPONENT_ID}`` environment variable is set to "20".


on job_init
-----------

When ``job_init`` of ``job_id`` 10 arrives, the following commands will be
constructued and issued to ``ldmsd``:

.. code::

  load name=sp0-procnetdev2-10 plugin=procnetdev2
  config name=sp0-procnetdev2-10 producer=node020 instance=node020/sp0-procnetdev2-10 component_id=20
  start name=sp0-procnetdev2-10 interval=1s offset=0

  stop name=meminfo
  start name=meminfo interval=0.1s offset=0

The first 3 lines are derived from the first action (``load_term``). The
``name`` default value is ``%N-%L-%J`` (policy name - plugin - job_id), which is
expanded to "sp0-procnetdev2-10". The default value of ``producer`` is the value
from ``gethostname()``, which is "node020" in this case. The ``instance``
default value is ``%P/%N-%L-%J``, which is expanded to
"node020/sp0-procnetdev2-10". The default ``interval`` is "1s", and the default
``offset`` is "0".

The last 2 lines are derived from the 2nd action (``interval_mod``) using the
interval from ``interval_mod["job_init"]``.


on job_exit
-----------

When ``job_exit`` of ``job_id`` 10 arrives, the following commands are issued to
``ldmsd``:

.. code::

  stop name=sp0-procnetdev2-10
  term name=sp0-procnetdev2-10

  stop name=meminfo
  start name=meminfo interval=1s offset=0

The first two lines are from the 1st action (``load_term``). Note that in this
case the plugin instance is immediately terminated because the ``term_delay`` is
not set (default: 0).

The last 2 lines are derived from the 2nd action (``interval_mod``) using the
interval from ``interval_mod["job_exit"]``.


Example 2
=========

This is an example that specifies all values.

.. code:: aconf

  msg_enable
  smplrp_add name=sp0 path=sp0.json
  smplrp_start name=sp0


``sp0.json`` file:

.. code:: python

  {
    "message_tag": "myslurm",
    "component_id": "${COMPONENT_ID}",
    "job_event_actions": [
      {
        "type": "load_term",
        "plugin": "procnetdev2",
        "name": "mynetdev-%J",
        "producer": "${HOSTNAMEJ}",
        "instance": "${HOSTNAME}/mynetdev-%$J",
        "interval": "2s",
        "offset": "0s",
        "term_delay": "10s",
        "config": {
          "ifaces": "lo,eth0"
        }
      },
      {
        "type": "interval_mod",
        "name": "vmstat",
        "job_init": { "interval": "0.1s", "offset": "0s"   },
        "job_exit": { "interval": "1s",   "offset": "0.1s" }
      },
      {
        "type": "interval_mod",
        "name": "procstat",
        "job_init": { "interval": "0.1s", "offset": "0s"   },
        "job_exit": { "interval": "1s",   "offset": "0.2s" }
      }
    ]
  }

In this example, ``sp0`` sampler policy subscribe for ``myslurm`` message tag.

To concretely explain what will happen on ``job_init`` and ``job_exit`` event,
let's suppose that:

  - ``ldmsd`` is running on ``node020``.

  - ``${COMPONENT_ID}`` environment variable is set to "20".


on job_init
-----------

When ``job_init`` of ``job_id`` 10 arrives, the following commands will be
constructued and issued to ``ldmsd``:

.. code::

  load name=sp0-procnetdev2-10 plugin=procnetdev2
  config name=sp0-procnetdev2-10 producer=node020 instance=node020/sp0-procnetdev2-10 component_id=20 ifaces=lo,eth0
  start name=sp0-procnetdev2-10 interval=2s offset=0s

  stop name=vmstat
  start name=vmstat interval=0.1s offset=0s

  stop name=procstat
  start name=procstat interval=0.1s offset=0s

The first chunk (3 lines) is from action 1 (``load_term``), the second chunk (2
lines) is from action 2 (``interval_mod`` on ``vmstat``), and the third chunk (2
lines) is from action 3 (``interval_mod`` on ``procstat``).


on job_exit
-----------

When ``job_exit`` of ``job_id`` 10 arrives, the following commands are issued to
``ldmsd``:

.. code::

  stop name=sp0-procnetdev2-10

  stop name=vmstat
  start name=vmstat interval=1s offset=0.1s

  stop name=procstat
  start name=procstat interval=1s offset=0.2s

The first chunk is from action 1 (``load_term``). This time, ``sp0`` only stops
the the instance because ``term_delay`` is set to ``10s``. The ``term`` command
is scheduled to execute 10s in the future.

The second chunk is from action 2 (``interval_mod`` on ``vmstat``), using
``interval`` from ``job_exit``.

The third chunk is from action 3 (``interval_mod`` on ``procstat``), using
``interval`` from ``job_exit``.

After 10 seconds has passed, ``term name=sp0-procnetdev2-10`` is issued to
``ldmsd``.

.. _geopm_sampler:

====================
geopm_sampler
====================

-----------------------------------
Man page for the LDMS geopm plugin
-----------------------------------

:Date:   06 May 2022
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| config name=ldms_geopm_sampler geopm_request_path=<path-to-file>

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), plugins for the
ldmsd (ldms daemon) are configured via ldmsd_controller or a
configuration file. The geopm plugin provides access to the :ref:`geopm(7) <geopm>`
PlatformIO interface by configuring the request file with signal
requests.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

The ldms_geopm_sampler plugin uses the sampler_base base class. This man
page covers only the configuration attributes, or those with default
values, specific to the this plugin; see :ref:`ldms_sampler_base(7) <ldms_sampler_base>` for the
attributes of the base class.

The GEOPM LDMS sampler can be configured with the same config parameters
as other LDMS samplers (e.g., \``name``, \``producer``,
\``component_Id``). In addition to these paramers, the sampler must be
configured with the option - \``geopm_request_path=<path-to-file>``.

**config**
   | name=<plugin_name> geopm_request_path=<path> [schema=<sname>]
   | configuration line

   name=<plugin_name>
      |
      | This MUST be ldms_geopm_sampler.

   geopm_request_path=<path>
      |
      | This parameter points to the absolute path of the ASCII file
        containing the list of signals that the user would like to have
        monitored by the sampler.

   The format of this file is a three column white space delimited file.
   Each line must contain a GEOPM PlatformIO request of the form:

   **<SIGNAL_NAME> <DOMAIN> <DOMAIN_INDEX>**

   The signal name must be a signal supported by GEOPM on the system. To
   see a full list of supported signals run the :ref:`geopmread(1) <geopmread>` command
   without any options. The domain must match one of the GEOPM domains.
   Run the :ref:`geopmread(1) <geopmread>` command with the -d option to see a full list of
   supported domains and the number of instances of each on the system.
   The domain index provided must be greater or equal to zero and less
   than the number of available domains.

   schema=<schema>
      |
      | Optional schema name. It is intended that the same sampler on
        different nodes with different metrics have a different schema.
        If not specified, will default to \`ldms_geopm_sampler`.

EXAMPLES
========

**CONFIGURING LDMSD WITH THE SAMPLER**

Within ldmsd_controller or a configuration file:

::

   load name=ldms_geopm_sampler
   config name=ldms_geopm_sampler producer=${HOSTNAME} geopm_request_path=/etc/ldms/geopm_sampler_request.config
   start name=ldms_geopm_sampler interval=1000000

Here's an example of a file containing the list of signals:

$> cat geopm_sampler_signal.config CPU_FREQUENCY_MAX board 0
CPU_FREQUENCY_MIN board 0 CPU_FREQUENCY_STEP board 0
CPU_FREQUENCY_STICKER board 0 TIME board 0 ENERGY_PACKAGE board 0
INSTRUCTIONS_RETIRED board 0 POWER_DRAM board 0 POWER_PACKAGE board 0
POWER_PACKAGE_LIMIT board 0 POWER_PACKAGE_MAX board 0 POWER_PACKAGE_MIN
board 0 POWER_PACKAGE_TDP board 0 TEMPERATURE_CORE board 0
TEMPERATURE_PACKAGE board 0 TIMESTAMP_COUNTER board 0

Note the inclusion of the *geopm_request_path* parameter passed to the
*config* instruction. Also, note the name of the sampler
*ldms_geopm_sampler* passed to the *name* parameter for the *load* and
*start* instructions.

**RUNNING LDMSD WITH THE SAMPLER**

In order to run the GEOPM LDMS sampler, follow the same steps as you
would for any other LDMS sampler. Start the \``ldmsd`\` daemon is
running on the target node to be monitored. Example below:

ldmsd -x sock:10444 -F -c <path-to-sampler-conf-script> -l
${TEST_PATH}/temp/demo_ldmsd_log

For observing the progress of the sampler, you may choose to add the
option \``-v DEBUG`\` above. While the \``ldmsd`\` daemon is running,
the user may choose to query for a single instantaneous sample set
comprising of recently monitored signals. This can be achieved by using
the existing commandline tool - \``ldms_ls`\` available as part of the
installation of the LDMS framework. An example is shown below:

::

        $> ldms_ls -h localhost -x sock -p 10444 -l -v

        Schema Instance Flags Msize Dsize Hsize UID GID Perm Update Duration
        Info -------------- ------------------------ ------ ------ ------ ------
        ------ ------ ---------- ----------------- ----------------- --------
        ldms_geopm_sampler <hostname>/ldms_geopm_sampler CL 1352 240 0 1024 100
        -r--r----- 1656431193.051578 0.000323 "updt_hint_us"="1000000:50000"
        -------------- ------------------------ ------ ------ ------ ------
        ------ ------ ---------- ----------------- ----------------- --------
        Total Sets: 1, Meta Data (kB): 1.35, Data (kB) 0.24, Memory (kB): 1.59

        =======================================================================

        <hostname>/ldms_geopm_sampler: consistent, last update: Tue Jun 28
        08:46:33 2022 -0700 [51578us] M u64 component_id 1 D u64 job_id 0 D u64
        app_id 0 D d64 CPU_FREQUENCY_MAX_board_0 3700000000.000000 D d64
        CPU_FREQUENCY_MIN_board_0 1000000000.000000 D d64
        CPU_FREQUENCY_STEP_board_0 100000000.000000 D d64
        CPU_FREQUENCY_STICKER_board_0 2100000000.000000 D d64 TIME_board_0
        6.899751 D d64 ENERGY_PACKAGE_board_0 334936.207092 D d64
        INSTRUCTIONS_RETIRED_board_0 131016700.000000 D d64 POWER_DRAM_board_0
        0.900889 D d64 POWER_PACKAGE_board_0 25.469352 D d64
        POWER_PACKAGE_LIMIT_board_0 140.000000 D d64 POWER_PACKAGE_MAX_board_0
        594.000000 D d64 POWER_PACKAGE_MIN_board_0 140.000000 D d64
        POWER_PACKAGE_TDP_board_0 280.000000 D d64 TEMPERATURE_CORE_board_0
        26.454545 D d64 TEMPERATURE_PACKAGE_board_0 28.000000 D d64
        TIMESTAMP_COUNTER_board_0 10913748924506.000000

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`geopm(7) <geopm>`, :ref:`geopm_pio(7) <geopm_pio>`, :ref:`geopmread(1) <geopmread>`, :ref:`geopmwrite(1) <geopmwrite>`

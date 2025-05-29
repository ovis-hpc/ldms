.. _store_csv:

================
store_csv
================

---------------------------------------
Man page for the LDMS store_csv plugin
---------------------------------------

:Date:   26 Nov 2018
:Manual section: 7
:Manual group: LDMS store

SYNOPSIS
========

| Within ldmsd_controller script or a configuration file:
| load name=store_csv
| config name=store_csv [ <attr> = <value> ]
| config name=store_csv [opt_file=filename] [ <attr> = <value> ]
| config name=store_csv [container=c schema=s] [ <attr> = <value> ]
| strgp_add name=<policyname> plugin=store_csv container=<c> schema=<s>
  [decomposition=<DECOMP_CONFIG_FILE_JSON>]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), store plugins for
the ldmsd (ldms daemon) are configured via the ldmsd_controller or a
configuration file. The store_csv plugin is a CSV store.

This plugin is multi-instance capable.

STORE_CSV CONFIGURATION SOURCES
===============================

Default configuration options can be defined on the config line or in
the store_csv line of the options file. Options for the specific
instance matching 'container=c schema=s" can be given in the file
indicated by opt_file=filename when configuring the defaults (see
section OPTIONS FILE below) or can be scripted.

The configuration parameters rolltype, rollover, and rollagain are
applied to all metric sets alike from the values given on the command
line or in the "store_csv" line of the options file. All other options
can be specified per instance.

The config defaults (a config line without container or schema defined)
can be specified once in scripting or the opt_file. They are used for
any container/schema pair not explicitly configured.

The config values for a specific container/schema pair can be specified
once in scripting or in the opt_file. Any attribute not specifically
defined will take on the value configured in the default config line or
opt_file.

STORE_CSV CONFIGURATION ATTRIBUTE SYNTAX
========================================

**config**
   | name=<plugin_name> path=<path> [ altheader=<0/!0>
     typeheader=<typeformat> time_format=<0/1> ietfcsv=<0/1>
     buffer=<0/1/N> buffertype=<3/4> rolltype=<rolltype>
     rollover=<rollover> rollempty=<0/1> userdata=<0/!0>]
     [rename_template=<metapath> [rename_uid=<int-uid>
     [rename_gid=<int-gid] rename_perm=<octal-mode>]]
     [create_uid=<int-uid>] [create_gid=<int-gid]
     [create_perm=<octal-mode>] [opt_file=filename] [ietfcsv=<0/1>]
     [typeheader=<0/1/2>] [array_expand=<false/true>]
     [array_sep=<separating char>] [array_lquote=<left-quote char>]
     [array_rquote=<right-quote char>]
   | ldmsd_controller configuration line

   name=<plugin_name>
      |
      | This MUST be store_csv.

   opt_file=<filename>
      |
      | The options for the plugin and specific instances will be read
        from the named file. See OPTIONS FILE.

   path=<path>
      |
      | This option is required; the config line or the options file
        must supply a default value. The output files will be put into a
        directory whose root is specified by the path argument. This
        directory must exist; the subdirectories and files will be
        created. The full path to the output files will be
        <path>/<container>/<schema>. Container and schema are set when
        the strgp is added. If you choose a rollover option, then the
        filename will also be postpended by "." followed by the epoch
        time e.g., XXX/meminfo_ctr/meminfo-123456789.

   altheader=<0/!0>
      |
      | Distinguishes whether or not to write the header to a separate
        file than the data file. 0 = same file. Any non-zero is a
        separate file. Default is the same file. If a separate file is
        used then, if the data file is called "meminfo" the additional
        header file will be called "meminfo.HEADER"). If you choose a
        rollover option, the header file name will be postpended with
        the epochtime, similar to the data file, and a new one will be
        written at each rollover. Default is altheader=0.

   typeheader=<typeformat>
      |
      | Controls the presence and format of a .KIND file. The kind CSV
        file gives type information on each metric (or metric array).
        For example, if the metric file is named meminfo, the kind file
        is named meminfo.KIND and if the metric file is named
        meminfo.15111111, the kind file is named meminfo.KIND.15111111.
        The typeformat parameter is 0 (no kind file), 1 (ldms kinds with
        arrays flattend out into scalars), 2 (LDMS kinds with arrays).
        The typeformat supporting arrays uses the notation
        <typename>[]<len> for extraction of lengths by scripting tools.
        The default typeformat is 0.

   time_format=<0/1>
      Controls the format of the initial time fields in each line of the
      CSV files.

   A value of 0 means the classic LDMS format where the first field
   (Time) is <seconds since epoch>.<microseconds>, and the second field
   (Time_usec) is <microseconds> repeated.

   A value of 1 chooses an alternate format where the first field
   (Time_msec) is <milliseconds since epoch>, and the second field
   (Time_usec) is just the additional <microseconds> since the epoch in
   excess of the milliseconds since epoch. In other words, there is no
   overlap of the values in the first and seconds fields, which is in
   contrast to the repetition employed by format 0.

   ietfcsv=<0/1>
      |
      | Turns on (1) or off (0) use of IETF 4180 quoting for header
        column names.

   userdata=<0/!0>
      |
      | Distinguishes whether or not to write each metrics' user data
        along with each data value. 0 = no write. Any non-zero means to
        write the values. Default is to not write.

   buffer=<0/1/N>
      |
      | Distinguishes whether or not to buffer the data for the
        writeout. 0 = does not buffer. 1 enables buffering with the
        system determining the flush. N will flush after approximately N
        kB of data (> 4) or N lines -- buffertype determines which of
        these it is. Default is system controlled buffering (1).

   buffertype=<3/4>
      |
      | If buffer=N then buffertype determines if the buffer parameter
        refers to kB of writeout or number of lines. The values are the
        same as in rolltype, so only 3 and 4 are applicable.

   rolltype=<rolltype>
      |
      | By default, the store does not rollover and the data is written
        to a continously open filehandle. Rolltype and rollover are used
        in conjunction to enable the store to manage rollover, including
        flushing before rollover. The header will be rewritten when a
        roll occurs. Valid options are:

      1
         |
         | wake approximately every rollover seconds and roll. Rollover
           is suppressed if no data at all has been written and
           rollempty=0.

      2
         |
         | wake daily at rollover seconds after midnight (>=0) and roll.
           Rollover is suppressed if no data at all has been written and
           rollempty=0.

      3
         |
         | roll after approximately rollover records are written.

      4
         roll after approximately rollover bytes are written.

      5
         |
         | wake at rollover seconds after midnight (>=0) and roll, then
           repeat every rollagain (> rollover) seconds during the day.
           For example "rollagain=3600 rollover=0 rolltype=5" rolls
           files hourly. Rollover is suppressed if no data at all has
           been written and rollempty=0.

   rollover=<rollover>
      |
      | Rollover value controls the frequency of rollover (e.g., number
        of bytes, number of records, time interval, seconds after
        midnight). Note that these values are estimates.

   rollempty=0
      |
      | Turn off rollover of empty files. Default value is 1 (create
        extra empty files).

   create_perm=<modebits>
      |
      | Only octal (e.g.0744) specifications are allowed. If unspecified
        or 0 is given, then no change is made. The default permission is
        0600 for data files. The mode specified can include execute bits
        which will apply to intermediate directories created but not
        data files. For example 0755 will yield 0755 for new directories
        and 0644 for data files.

   create_uid=<numeric-uid>
      |
      | Specify a new user id for data files. If unspecified, no change
        in user ownership is made. Changes in ownership of the files do
        not affect intermediate directories.

   create_gid=<numeric-gid>
      |
      | Specify a new group id for data files. If unspecified, no change
        in group ownership is made.

   rename_template=<metapath>
      |
      | This option relocates closed CSV files, typically to a
        subdirectory, for processing by other tools that watch
        directories. The metapath template is applied to define a new
        name after file closure. The rename is limited to locations on
        the same mount point, per the C :ref:`rename(2) <rename>` call. Substitutions
        (%) in the provided template are performed as described in
        METAPATH SUBSTITUTIONS below. Errors in template specification
        will cause the rename to be skipped. As part of the renaming
        process, the mode and ownership of the file may also be adjusted
        by specifying rename_perm, rename_uid, and rename_gid. Missing
        intermediate directories will be created if possible. To enable
        greater flexibility than the renaming just described (e.g.
        crossing file systems), an external program must monitor the
        output directory and handle completed files.

   rename_perm=<modebits>
      |
      | Only octal (e.g.0744) specifications are allowed. If unspecified
        or 0 is given, then no change is made. The permissions are
        changed before the rename and even if the rename fails. This
        option is applied only if rename_template is applied.

   rename_uid=<numeric-uid>
      |
      | Specify a new user id for the file. If unspecified, no change in
        user ownership is made. Changes in ownership of the files do not
        affect intermediate directories that might be created following
        the template. This option is applied only if rename_template is
        applied.

   rename_gid=<numeric-gid>
      |
      | Specify a new group id for the file. If unspecified, no change
        in group ownership is made. This option is applied only if
        rename_template is applied.

   expand_array=<true/false>
      |
      | The default is false. Each array element is stored in a column.
        True means that all elements are stored in a single column.

   array_sep=<char>
      |
      | Specify a character to separate array elements. If exand_array
        is true, the value is ignored.

   array_lquote=<char>
      |
      | Specify the left-quote character if expand_array is true. If
        expand_array is false, the value is ignored.

   array_rquote=<char>
      |
      | Specify the right-quote character if expand_array is true. If
        expand_array is false, the value is ignored.

OPTIONS FILE
============

The plug-in options file or repeated scripted config calls replace the
LDMS v3 'action' keyword for defining instance specific settings.

The options file recognizes lines starting with # as comments.
Continuation lines are allowed (end lines with a \\ to continue them).
Comment lines are continued if ended with a \\. See EXAMPLES below.

When an option is needed for a plugin instance, the content of the
options file is searched beginning with the options line holding
"container=$c schema=$s". If the matching container/schema is not found
in the options file or the option is not defined among the options on
that line of the file, then the option value from the ldmsd script
'config' command line is used. If the option is not set on the command
line, the defaults are taken from the line of the options file
containing the keyword 'store_csv'. If the option is found in none of
these places, the compiled default is applied.

STRGP_ADD ATTRIBUTE SYNTAX
==========================

The strgp_add sets the policies being added. This line determines the
output files via identification of the container and schema.

**strgp_add**
   | plugin=store_csv name=<policy_name> schema=<schema>
     container=<container> [decomposition=<DECOMP_CONFIG_FILE_JSON>]
   | ldmsd_controller strgp_add line

   plugin=<plugin_name>
      |
      | This MUST be store_csv.

   name=<policy_name>
      |
      | The policy name for this strgp.

   container=<container>
      |
      | The container and the schema determine where the output files
        will be written (see path above). They also are used to match
        any specific config lines.

   schema=<schema>
      |
      | The container and the schema determine where the output files
        will be written (see path above). You can have multiples of the
        same sampler, but with different schema (which means they will
        have different metrics) and they will be stored in different
        containers (and therefore files).

   decomposition=<DECOMP_CONFIG_FILE_JSON>
      |
      | Optionally use set-to-row decomposition with the specified
        configuration file in JSON format. See more about decomposition
        in :ref:`ldmsd_decomposition(7) <ldmsd_decomposition>`.

STORE COLUMN ORDERING
=====================

This store generates output columns in a sequence influenced by the
sampler data registration. Specifically, the column ordering is

   Time, Time_usec, ProducerName, <sampled metric >\*

where each <sampled metric> is either

   <metric_name>.userdata, <metric_name>.value

or if userdata has been opted not to include, just:

   <metric_name>

The column sequence of <sampled metrics> is the order in which the
metrics are added into the metric set by the sampler (or the order they
are specifed by the user).

Note that the sampler's number and order of metric additions may vary
with the kind and number of hardware features enabled on a host at
runtime or with the version of kernel. Because of this potential for
variation, down-stream tools consuming the CSV files should always
determine column names or column number of a specific metric by parsing
the header line or .HEADER file.

METAPATH SUBSTITUTION
=====================

The following % escape sequence replacements are performed on the
rename_template value for file renamings:

%P
   |
   | plugin name

%C
   |
   | container name

%S
   |
   | schema name

%T
   |
   | file type (DATA, HEADER, KIND, UNITS, CNAMES, PYNAMES)

%B
   |
   | basename(closed-file-name)

%D
   |
   | dirname(closed-file-name)

%{ENV_VAR_NAME}
   |
   | getenv(ENV_VAR_NAME). The use of undefined or empty environment
     vars yields an empty substitution, not an error. Characters in the
     environment variable are restricted to: 'A-Za-z0-9%@()+-_./:=';
     other characters present will prevent the rename.

%s
   |
   | timestamp suffix, if it exists.

NOTES
=====

-  Please note the argument changes from v2 and v3. The notification of
   file events has be removed, being redundant with renaming closed
   files into a spool directory.

-  The 'sequence' option has been removed. The 'action' option has been
   replaced; see "OPTIONS FILE" above.

-  In the opt_file passed by name to store_csv, including the line
   prefix "config name=store_csv" is redundant and is disallowed. The
   opt_file syntax is plugin specific and is not an ldmsd configuration
   script. Scripts written in the store_csv opt_file syntax cannot be
   used directly with the ldmsd include statement.

BUGS
====

None known.

IMPERFECT FEATURES
==================

The rename and create options do not accept symbolic permissions, uid,
or gid. There is no metapath substitution for file creation.

EXAMPLES
========

Within ldmsd_controller or in a ldmsd command script file

::

   load name=store_csv
   config name=store_csv opt_file=/etc/sysconfig/ldms.d/store-plugins/store_csv.conf
   strgp_add name=csv_mem_policy plugin=store_csv container=loadavg_store schema=loadavg

Or with interactive modifications to override file properties:

::

   load name=store_csv
   config name=store_csv altheader=1 rolltype=2 rollover=0 path=/mprojects/ovis/ClusterData/${LDMSCLUSTER} create_gid=1000000039 create_perm=640 rename_template=%D/archive-spool/%{HOSTNAME}/%B rename_perm=444

And in the options file for store_csv
(/etc/sysconfig/ldms.d/store-plugins/store_csv.conf by convention)

::

   # defaults for csv, unless overridden on ldmsd script config line.
   store_csv altheader=1 path=/XXX/storedir rolltype=2 rollover=0
   # tailored setting for loadavg instance
   container=loadavg_store schema=loadavg altheader=0 path=/XXX/loaddir \
    create_gid=1000000039 create_perm=640 \
    rename_template=%D/archive-spool/%{HOSTNAME}/%B \
    rename_perm=444

Updating from v3:

If in version 3 "config name=store_csv action=custom container=cstore
schema=meminfo" was used for a specific csv instance, then put the
additional options for that store instance in the store_csv options file
on a line:

container=cstore schema=meminfo <op=val >\*

or use them interactively or in a script as:

config name=store_csv container=cstore schema=meminfo <op=val >\*

after the store_csv defaults have been set.

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`,
:ref:`ldmsd_decomposition(7) <ldmsd_decomposition>`

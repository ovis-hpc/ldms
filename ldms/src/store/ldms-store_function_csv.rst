.. _store_function_csv:

=========================
store_function_csv
=========================

-----------------------------------------------
Man page for the LDMS store_function_csv plugin
-----------------------------------------------

:Date:   22 Aug 2017
:Manual section: 7
:Manual group: LDMS store


SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| load name=store_function_csv
| config name=store_function_csv [ <attr> = <value> ]
| strgp_add plugin=store_function_csv [ <attr> = <value> ]

DESCRIPTION
===========

With LDMS (Lightweight Distributed Metric Service), store plugins for
the ldmsd (ldms daemon) are configured via the ldmsd_controller. The
store_function_csv plugin is a CSV store.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

**config**
   | name=<plugin_name> path=<path> derivedconf=<confname>[
     altheader=<0/!0> ageusec=<ageusec> buffer=<0/1/N> buffertype=<3/4>
     rolltype=<rolltype> rollover=<rollover>]
   | ldmsd_controller configuration line

   name=<plugin_name>
      |
      | This MUST be store_function_csv.

   path=<path>
      |
      | The output files will be put into a directory whose root is
        specified by the path argument. This directory must exist; the
        subdirectories and files will be created. The full path to the
        output files will be <path>/<container>/<schema>. Typically
        these are chosen to make output files like:
        XXX/store_function_csv/meminfo. If you choose a rollover option,
        then the filename will also be postpended by "-" followed by the
        epochtime e.g., XXX/store_function_csv/meminfo-123456789.

   derivedconf=<confname>
      |
      | Path to the derived configuration file(s). If more than 1 is
        given, separate by commas. Format information is given elsewhere
        in this document.

   altheader=<0/!0>
      |
      | Distinguishes whether or not to write the header to a separate
        file than the data file. 0 = same file. Any non-zero is a
        separate file. Default is the same file.If a separate file is
        used then, if the data file is called "meminfo" the additional
        header file will be called "meminfo.HEADER"). If you choose a
        rollover option, the header file name will be postpended with
        the epochtime, similar to the data file, and a new one will be
        written at each rollover. Default is altheader=0.

   ageusec=<ageusec>
      |
      | Set output data field Flag = 1 if the interval between two
        successive data reports for the same host's same metric set is
        greater than ageusec. (NOTE: in v3 this is now usec, not sec).

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

   rollover=<rollover>
      |
      | Rollover value controls the frequency of rollover (e.g., number
        of bytes, number of records, time interval, seconds after
        midnight). Note that these values are estimates.

   rollempty=0
      |
      | Turn off rollover of empty files. Default value is 1 (create
        extra empty files).

STORE_FUNCTION_CSV CONFIGURATION FILE
=====================================

Format: schema new_metricname function num_dependent_metrics
csv_list_of_dependent_metrics scale|thresh writeout.

Whitespace delimits the fields in the configuration file; commas delimit
the metric this new metric is dependent upon. One metric per line. No
spaces are allowed within a metric name.

   schema
      |
      | The schema for this new metric. It will be dependent upon base
        metrics from this schema and/or new metrics which derive from
        these base metrics.

   new_metricname=<name>
      |
      | The name for this new metric. The name for a derived metric can
        be the same as that as a base metric (e.g., one provided
        innately by the metric set). Note that when searching the
        csv_list_of_dependent_metrics, the base metrics will searched
        before the derived metrics for a matching metric name. This is
        not to be relied on however; we recommend that you only reuse a
        metric name for the RAWTERM function.

   function=<fct>
      |
      | Identify the function for the calculation. Options are defined
        below.

   num_dependent_metrics
      |
      | Number of metrics that this new metric is derived from.

   csv_list_of_dependent_metrics
      |
      | Depedent metricname and schema uniquely identify the variable to
        match. Thus, if you have the same sampler on two different nodes
        having different schema, you can create a different derived
        metric for each. The dependent metrics must all belong to the
        same schema, or be derived metrics which are then based on the
        base metrics of the same schema.

   There is a special option for handling base msr_interlagos metrics.
   These metrics have a generic name, CtrN_c or CtrN_n, with a special
   metric, CtrN_name, that is a char array of the counter name. You can
   use CtrN (e.g., CtrN7_n) as a metric in the csv list in the usual
   way. You can also use the countername:BYMSRNAME in the csv list
   instead, in which case the correct numbered metric will be
   discovered. Note that the dependencies are discovered only when
   reading the config. If the metrics or metric order change later, the
   associations will not be redetermined.

   scale|thresh
      |
      | A float scale value or thresh value is included as part of every
        function. Value is scale except for thresh functions. Use 1 if
        you want no scale. Currently the details of the scale
        multiplication are being worked out.

   writeout
      |
      | Values of either 0 or 1 depending on whether or not this metric
        should be written out to the store (it may be an intermediate in
        a calculation).

Derived configuration format example:

::

   # SCHEMA NEW_METRICNAME FUNCTION N_MET <METS_CSV> SCALE|THRESH WRITEOUT
   meminfo RAW_ACTIVE RAW 1 Active 1 1
   meminfo DELTA_ACTIVE DELTA 1 Active 1 1
   meminfo RATE_ACTIVE RATE 1 Active 1 1

   meminfo ACT_TOT DIV_AB 2 Active,Total 1 1
   meminfo R_ACT_TOT RATE 1 ACT_TOT 1 1

   msr_interlagos flop_raw RAW 1 RETIRED_FLOPS:BYMSRNAME 1 0
   msr_interlagos flop_v_rate RATE 1 flop_raw .000001 0
   msr_interlagos flop_rate SUM 1 flop_v_rate 1 1

Blank lines are allowed in the file as shown

SUPPORTED FUNCTIONS
===================

RAW
   |
   | The raw value. This function is univariate. It operates on either a
     uint64_t or a vector of uint64_t. It returns the same type as it
     operates upon.

DELTA
   |
   | The difference between the current value and the last. This
     function is univariate. It operates on either a uint64_t or a
     vector of uint64_t. It returns the same type as it operates upon.

RATE
   |
   | The difference between the current value and the last divided by
     the time. This function is univariate. It operates on either a
     uint64_t or a vector of uint64_t. It returns the same type as it
     operates upon.

SUM_N
   |
   | The sum of N inputs. This function is multivariate. It operates on
     uint64_t's or a vectors of uint64_t. It returns the same type as it
     operates upon.

AVG_N
   |
   | The avg of N inputs. This function is multivariate. It operates on
     uint64_t's or a vectors of uint64_t. It returns the same type as it
     operates upon.

SUB_AB
   |
   | Subtract two inputs in the order they are listed. This function is
     bivariate. It operates on two uint64_t's or two vectors of
     uint64_t. It returns the same type as it operates upon.

MUL_AB
   |
   | Multiplies two inputs. This function is bivariate. It operates on
     two uint64_t's or two vectors of uint64_t. It returns the same type
     as it operates upon.

DIV_AB
   |
   | Divides input A by input B, in the order they are listed. This
     function is bivariate. It operates on two uint64_t's or two vectors
     of uint64_t. It returns the same type as it operates upon.

THRESH_GE
   |
   | Returns 1 or 0 if a value is greater or equal to some threshold,
     specified by the scale value. This function is univariate. It
     operates on a uint64_t or a vector of uint64_t. It returns the same
     type as it operates upon.

THRESH_LT
   |
   | Returns 1 or 0 if a value is greater or equal to some threshold,
     specified by the scale value. This function is univariate. It
     operates on a uint64_t or a vector of uint64_t. It returns the same
     type as it operates upon.

MAX
   |
   | Returns the max value. This function is univariate. It operates on
     a uint64_t or, most likely, a vector of uint64_t in which case it
     returns the max of all the values in the vector. It returns a
     uint64_t.

MIN
   |
   | Returns the min value. This function is univariate. It operates on
     a uint64_t or, most likely, a vector of uint64_t in which case it
     returns the min of all the values in the vector. It returns a
     uint64_t.

SUM
   |
   | Returns the sum. This function is univariate. It operates on a
     uint64_t or, most likely, a vector of uint64_t in which case it
     returns the SUM over all the values in the vector. It returns a
     uint64_t.

AVG
   |
   | Returns the avg. This function is univariate. It operates on a
     uint64_t or, most likely, a vector of uint64_t in which case it
     returns the avg of all the values in the vector. It returns a
     uint64_t.

SUM_VS
   |
   | Returns the sum of a vector and scalar value applied to each value
     in the vector. It operates on a vector of uint64_t and a scalar
     uint64_t specified in that order. It returns a vector of uint64_t
     of the same size as the input vector.

SUB_VS
   |
   | Returns the value of a scalar subtracted from each value of the
     vector. The vector and the scalar are specified in that order. The
     scalar and vector are of type uint64_t. It returns a vector of
     uint64_t of the same size as the input vector.

SUB_SV
   |
   | Returns a vector where each value is that of the difference of a
     scalar and an individual value of a vector. The scalar and the
     vector are specified in that order. The scalar and vector are of
     type uint64_t. It returns a vector of uint64_t of the same size as
     the input vector.

MUL_VS
   |
   | Returns the value of each value of a vector multiplied by a scalar.
     The vector and the scalar are specified in that order. The scalar
     and vector are of type uint64_t. It returns a vector of uint64_t of
     the same size as the input vector.

DIV_VS
   |
   | Returns the value of a each value of vector divided by a scalar.
     The vector and the scalar are specified in that order. The scalar
     and vector are of type uint64_t. It returns a vector of uint64_t of
     the same size as the input vector.

DIV_SV
   |
   | Returns the value of a scalar divided by each value of a vector.
     The scalar and the vector are specified in that order. The scalar
     and vector are of type uint64_t. It returns a vector of uint64_t of
     the same size as the input vector.

STORE COLUMN ORDERING
=====================

This store generates output columns in a sequence influenced by the
sampler data registration. Specifically, the column ordering is

   Time, Time_usec, DT, DT_usec, ProducerName, <new_metric >*,
   <new_metric.flag >*,Flag

Flag will be set if a) the dt is negative b) dt is greater than ageusec
or c) in a rate or delta calculation, the second value is greater than
the first. It is NOT set if the cast in the computation would result in
an overflow.

The column sequence of <new_metrics> is the order in which the metrics
are added into the metric set by the derived csv store configuration
file.

STRGP_ADD ATTRIBUTE SYNTAX
==========================

The strgp_add sets the policies being added. This line determines the
output files via identification of the container and schema.

**strgp_add**
   | plugin=store_function_csv name=<policy_name> schema=<schema>
     container=<container>
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
        will be written (see path above).

   schema=<schema>
      |
      | The container and the schema determine where the output files
        will be written (see path above). The schema is also used to
        match the metric-schema combinations identified in the derived
        configuration file.

NOTES
=====

-  A metric must be specified before it can be used as part of another
   metric.

-  Spaces in metric names are not supported.

-  Derived metrics may be used as input into other metrics.

-  The name for a derived metric can be the same as that as a base
   metric (e.g., one provided innately by the metric set). Note that
   when searching the csv_list_of_dependent_metrics, the base metrics
   will searched before the derived metrics for a matching metric name.
   This is not to be relied on however; we recommend that you only reuse
   a metric name for the RAWTERM function.

-  Note that the dependencies are discovered only when reading the
   config. If the metrics or metric order change later, the associations
   will not be redetermined.

-  Although scale is a float option, its placement in the calculation is
   being worked out. In the meantime, it may be cast into a uint64_t as
   part of the calculation.

-  Thresh and scale currently use the same variable. Thresh may change
   to a uint64_t to match the variable types later.

-  Flag will be set if a) the dt is negative or b) dt is greater than
   ageusec. Individual variable flags will be set if a) there is invalid
   input to the calculation or b) in a rate or subtraction calculation,
   the second value is greater than the first. It is NOT set if the cast
   in the computation would result in an overflow.

-  This store is speculative at the moment. This store replaces
   store_derived_csv.

BUGS
====

None.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=store_function_csv
   config name=store_function_csv altheader=1 derivedconf=/XXX/der1.conf,/XXX/der2.conf path=/XXX/storedir
   strgp_add name=csv_memRHeL6_policy plugin=store_function_csv container=data_der schema=meminfoRHeL6
   strgp_add name=csv_memRHeL7_policy plugin=store_function_csv container=data_der schema=meminfoRHeL7
   strgp_add name=csv_ps_policy plugin=store_function_csv container=data_der schema=procstat

SEE ALSO
========

:ref:`ldms(7) <ldms>`, :ref:`store_csv(7) <store_csv>`, :ref:`msr_interlagos(7) <msr_interlagos>`

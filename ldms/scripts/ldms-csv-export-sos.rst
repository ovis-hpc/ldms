.. _ldms-csv-export-sos:

===================
ldms-csv-export-sos
===================

-----------------------------------------------
Generate helper files needed by sos-import-csv
-----------------------------------------------

:Date:   18 Apr 2019
:Manual section: 1
:Manual group: LDMS scripts

SYNOPSIS
========

ldms-csv-export-sos -h

ldms-csv-export-sos [--data DATA] [--blacklist BLACKLIST] [--whitelist
WHITELIST] [--exclude EXCLUDE] [--include INCLUDE] [--schema-name
SCHEMA_NAME] [--schema-file SCHEMA_FILE] [--map-file MAP_FILE]
[--strip-udata] [--guess] [--widen] [--maxlines MAXLINES] [--assume
ASSUME] [--verbose]

DESCRIPTION
===========

The ldms-csv-export-sos command parses LDMS CSV file information to
generate corresponding map (and optionally schema) files used by
sos-import-csv.

OPTIONS
=======

--data=<DATA>
   |
   | DATA is a file name of a LDMS .HEADER, .KIND, or data file. The
     file name and at least the first line of the file are digested to
     determine the content and the column types. LDMS CSV file name
     conventions ($schema[.$date] is associated with
     $schema.HEADER.$date or $schema.KIND.$date in the same directory).
     The file may be gzipped; if so, the matching data/HEADER/KIND files
     must also be gzipped.

--blacklist=<BLACKLIST>
   |
   | BLACKLIST is the name of a file with column names to exclude from
     the schema, one per line. leading # comments allowed in the file.

--whitelist=<WHITELIST>
   |
   | WHITELIST is the name of a file with column names to include in the
     schema, one per line. leading # comments allowed in the file. Any
     other columns found are excluded.

--exclude=<LIST>
   |
   | LIST is a string of metric names separated by commas. Columns named
     are excluded from the generated schema.

--include=<LIST>
   |
   | LIST is a string of metric names separated by commas. Columns named
     are included in the generated schema and all other columns found
     are excluded.

--schema-name=<NAME>
   |
   | NAME overrides the default schema name determined from the data
     file name.

--schema-file=<FILE>
   |
   | Use an existing schema file FILE instead of generating a schema.
     When not specified, a schema file is always generated. Schema files
     may not be gzipped.

--map-file=<MAP_FILE>
   |
   | Override the output map file name derived from the data file name.

--alias-file=<ALIASES>
   |
   | Provide the list of metrics to rename when creating or matching a
     schema discovered from a header line.

--strip-udata
   |
   | Suppress output of .userdata fields and remove .value suffix from
     schema element names.

--guess
   |
   | Guess the ldms data column types. (can be slow on large files)

--maxlines=<MAXLINES>
   |
   | Parse no more than MAXLINES to guess data types with the --guess
     option. The default if unspecified is 100000 lines.

--assume=<ASSUME>
   |
   | Assume all unknown data columns are type ASSUME.

--verbose
   |
   | Show process debugging details.

--widen
   |
   | Widen numeric types discovered to 64 bits.

METRIC FILTERING
================

When an include or whitelist is specified, exclude and blacklist
arguments are ignored entirely. An include option cannot be used to
prune a blacklist file.

When userdata is present in the CSV file, for these filters, metric
names should be written without the .value or .userdata suffix.

NOTES
=====

The recommended export method is to use the .KIND file if available and
to use the options "--guess --widen --maxlines=2" for legacy LDMS files.
This tool is aware of the CSV conventions (up to LDMS v4) for columns
named Time, ProducerName, producer, compid, component_id, Time_usec,
DT_usec, jobid, job_id, app_id, uid, and names ending in .userdata.

Both assume and guess options should be used judiciously. Know your data
before using SOS or any other database. The output schema file is
formatted for editability, and it should be adjusted before use with SOS
if any guess or assumption proves erroneous.

BUGS
====

There is no pipeline filtering mode.

EXAMPLES
========

To test sos-import-csv with the resulting files:

::


   ldms-csv-export-sos --data=renamecsv.1553744481 \
    --strip-udata --schema-name=meminfo \
    --blacklist=exclude.renamecsv

   mkdir container
   sos-db --path container --create
   sos-schema --path container \
    --add renamecsv.SCHEMASOS.1553744481
   sos-import-csv \
    --path container \
    --csv renamecsv.1553744481 \
    --map renamecsv.MAPSOS.1553744481 \
    --schema meminfo \
    --status
   sos_cmd -C container -l
   sos_cmd -C container -q -S meminfo -X Time

Other examples

::


   # make schema and map from *81 with schema rename from file
   ldms-csv-export-sos --data=renamecsv.1553744481 \
    --strip-udata --schema-name=meminfo \
    --blacklist=exclude.renamecsv

   # reuse schema and make map from *90
   ldms-csv-export-sos --data=renamecsv.1553744490 \
    --schema-file=renamecsv.SCHEMASOS.1553744481

   # reuse schema and make map from *90 with alternate output name
   ldms-csv-export-sos --data=renamecsv.1553744490 \
    --strip-udata \
    --schema-file=renamecsv.SCHEMASOS.1553744481 \
    --map-file=mymap

   # translate array example (when supported)
   ldms-csv-export-sos --data=fptrans.HEADER --strip-udata

   # translate array with old schema (when supported)
   ldms-csv-export-sos --data=fptrans2.HEADER \
    --schema-file=fptrans.SCHEMASOS

   # test input guess when x.14 does not exist
   ldms-csv-export-sos --data=x.HEADER.14 --guess

   # test input guess when y.KIND.14 does not exist but y.14 does
   ldms-csv-export-sos --data=y.HEADER.14 \
    --guess --maxlines=4000

   # test input guess and widen
   ldms-csv-export-sos --data=y.HEADER.14 \
    --guess --widen --maxlines=4

   # test assume
   ldms-csv-export-sos --data=y.HEADER.14 --assume=u32

SEE ALSO
========

:ref:`sos-import-csv(1) <sos-import-csv>`, :ref:`ldms-csv-export-sos(1) <ldms-csv-export-sos>`

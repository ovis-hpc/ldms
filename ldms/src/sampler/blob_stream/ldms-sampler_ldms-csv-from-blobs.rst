.. _ldms-csv-from-blobs:

===================
ldms-csv-from-blobs
===================

-------------------------------------------------------------------
Generate schema-consistent csv files from blob_stream_writer output
-------------------------------------------------------------------

:Date:   21 Apr 2023
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

ldms-csv-from-blobs -h

ldms-csv-from-blobs [--struct-map STRUCT_MAP_FILE] [--flatten true]
[--crc-map CRC_MAP_FILE] FILES

DESCRIPTION
===========

The ldms-csv-from-blobs command parses blob_stream_writer output to
generate corresponding LDMS CSV files. Unique CSV schema are detected
and separated. The input files are .DAT. files from the
blob_stream_writer. The auxiliary files (.TYPES., .OFFSET.) are assumed
to be adjacent to the .DAT. files; reasonable guesses are applied in
their absence.

Nested dictionaries are transformed into a set of columns with
dot-qualified names based on appending the keys. Lists are partially
supported, in that only the first element is used; see NOTES below.

OPTIONS
=======

--struct-map STRUCT_MAP_FILE
   |
   | STRUCT_MAP_FILE is the name of a JSON file that defines non-default
     mapping of fields within any json blob type. See STRUCT MAPPING
     below.

--crc-map CRC_MAP_FILE
   |
   | CRC_MAP_FILE is the name of a JSON file that defines mapping from a
     CRC32

--flatten true
   |
   | Automatically strip prefixes from dot-qualified names. This may
     generate multiple columns with the same name if inner and outer
     JSON dictionaries use the same keyword.

STRUCT MAPPING
==============

JSON field names are converted to CSV column names. Nested JSON
structures yield dot-qualified column names unless the dot qualified
name is remapped or flattened.

Field names are remapped by defining '"alias":"REPLACEMENT"'

Field names can be labeled with '"delete":true' to suppress them from
CSV output.

LDMS field types can be defined with '"kind":LDMSTYPE' where LDMSTYPE is
one of the strings: u8, u16, u32, u64, s8. s16, s32, s64, timestamp,
char, f32, d64, or one of these types suffixed with [] to indicate an
array.

Array size bounds can be defined with "count":SIZE' where SIZE is an
integer.

The following example replaces dot-qualified names, fixes string sizes,
determines a signed-vs-unsigned ambiguity caused by JSON in
component_id, and suppresses the field data.nycount if it is seen. It
also includes a commenting strategy example.

::


   {
     ",comment1" : { "purpose": "map items to proper types and desired names, before schema crc computation",
       "alias": "used to rename (possibly nested) keys. (optional, default to original name)",
       "kind": "required: ldms type (string,int,float,unsigned types have no good default guess)",
       "count": "optional: length of array types (defaults to 1)",
       "delete": "optional: value true causes the item to be suppressed in csv output (default false)"
     },
     "timestamp": { "alias": "Timestamp", "kind": "timestamp" },
     "data.ProducerName": { "alias": "ProducerName",
                            "kind": "char[]", "count": 64 },
     "data.exe": { "alias": "exe", "kind": "char[]", "count": 256 },
     "data.component_id": { "alias": "component_id", "kind": "u64" },
     "data.cluster": { "alias": "cluster", "kind": "char[]", "count": 16 },
     "data.task_exit_status": { "alias": "task_exit_status",
                                "kind": "char[]", "count": 16 },
     "data.job_name": { "alias": "job_name", "kind": "char[]", "count": 256 },
     "data.job_user": { "alias": "job_user", "kind": "char[]", "count": 32 },
     "data.job_id": { "alias": "job_id", "kind": "char[]", "count": 32 },
     "schema": { "alias": "schema", "kind": "char[]", "count": 32 },
     "event": { "alias": "event", "kind": "char[]", "count": 32 },
     "context": { "alias": "context", "kind": "char[]", "count": 32 },
     "data.nycount": { "delete":true }
   }

CRC_MAP_FILE
============

::


   { "comment1" : "table of crc32 aliases as computed _after_ struct_map is applied",
     "comment2" : "used to replace CRC_XXX in output file names",
     "comment3" : "CRC_XXX appears in output file names if no mapping is found here",
     "comment4" : "CRC variations most often occur due to int/float/unsigned vagueness",
     "comment5" : "  or due to string length variations which must be forced to an upper-bound",
     "1460183583": "slurm_step_exit",
     "2487927410": "slurm_task_exit",
     "3034079189": "slurm_task_init",
     "3171915531": "slurm_job_exit",
     "414824126": "slurm_job_init",
     "3201332233": "slurm_step_init"
   }

OUTPUT
======

Multiple data and header files are created, and individual blobs which
cause errors are ignored. Informational messages appear on the standard
output. For example:

::

   Processing $FILE
   total offsets: 53
   total types: 53
   total blobs: 53
   Ignored 272 elements from 51 blobs
   array ProducerName maximum 256
   array job_id maximum 32
   array k maximum 16
   array v maximum 256

indicates consistent blob, offset, and type data were found with 53
blobs in each. 51 of these blobs contained a list of dictionaries, and a
total of 272 elements in these lists were ignored. The array identifiers
found had data values with corresponding maximum sizes seen; these may
be used to update a struct-map file.

EXAMPLES
========

To convert a blob file containing items from the spank event plugin
slurm stream:

::


   ldms-csv-from-blobs --struct-map ./struct-map.json \
    --crc-map ./crc-map.json \
    /dataroot/blobs/slurm.DAT.1682105426

NOTES
=====

The CRC32 value computed is based on the header content (schema) of the
output CSV.

The input transformation is based only on individual field names, not on
the overall content of each JSON object transformed.

By design, only the first element of any list is processed and the rest
are skipped. For complicated message structures that need unrolling of
lists to multiple CSV rows, replay the messages to an ldmsd configured
with an appropriate storage policy decomposition rule set.

SEE ALSO
========

:ref:`store_csv(7) <store_csv>`

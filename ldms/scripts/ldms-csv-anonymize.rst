.. _ldms-csv-anonymize:

==================
ldms-csv-anonymize
==================

-------------------------------
Anonymize columns of csv files
-------------------------------

:Date:   18 Apr 2019
:Manual section: 1
:Manual group: LDMS scripts

SYNOPSIS
========

ldms-csv-anonymize -h

ldms-csv-anonymize [--input csv-file] [--out-dir OUT_DIR] [--col-sep
COL_SEP] [--seed SEED] [--save-maps SAVE_MAPS] [--imap IMAP] [--nmap
NMAP] [--pmap PMAP] [--hmap HMAP] [--debug] [M:C [M:C ...]]

ldms-csv-anonymize --gen-args GEN_ARGS <header_file>

DESCRIPTION
===========

The ldms-csv-anonymize command rewrites ldms and slurm data files
column-wise with filters specified by the M:C arguments. M:C is a
mapping:column number pair or filename. M is one of int,path,name,host.
C is a nonzero number. Negative numbers count back from the last column.

OPTIONS
=======

--input=<args>
   |
   | Args is a file name or space-separated list of file names to be
     processed. Filenames cannot contain whitespace.

--out-dir=<path>
   |
   | Path is a directory (must pre-exist and should not be the same as
     any directory containing the input) which will be filled with the
     changed files. The original files will are not changed. If an
     output file name coincides with one of the inputs, the input data
     may be lost or corrupted.

--col-sep=<character>
   |
   | Split columns at this character. The default is comma.

--save-maps=<prefix>
   |
   | The path prefix for the generated map files. If the resulting map
     filenames coincide with an existing file, the existing file is
     overwritten.

--imap=<file>
   |
   | An integer mapping file to preload. It must contain two columns of
     integers and magic. Normally it is the output of a prior run. See
     MAPS below.

--nmap=<file>
   |
   | A name mapping file to preload. It must contain two columns of
     names and magic. Normally it is the output of a prior run. Each
     real name is replaced with 'n' and a sequential number. See MAPS
     below.

--pmap=<file>
   |
   | A path element mapping file to preload. It must contain two columns
     of path elements and magic. Normally it is the output of a prior
     run. Path elements are unqualified subdirectory names. Each unique
     subdirectory name is replaced with 'p' and a sequential number,
     allowing directory hierarchy to be preserved without revealing
     application identities. See MAPS below.

--hmap=<file>
   |
   | A host name mapping file to preload. It must contain columns of
     host elements and magic. It may be host name fragment information
     or the output of a prior run. Any hostname found in the input data
     which cannot be mapped to the host elements will cause an
     anonymization error. There is no default handling of unknown hosts.
     See MAPS below.

--gen-args=<M:H>[,M:H]*,<header_file_name>
   |
   | Creating the M:C specification needed in a data transformation run
     can be done by first using the argument generation mode. Given a
     file starting with a header line of column names and the list of
     method:name pairs, this command displays the corresponding list of
     M:C arguments needed for the data transformation.

--debug
   |
   | Echo some details of the transformation as it runs.

--seed
   |
   | Supply a seed to the random number generator. No random values are
     used at this time in the processing, however.

MAPS and MAGIC
==============

Map files all start with a line of the form "#anonymize-csv-map <kind>"
where kind is one of the supported M values. The columns of the file are
separated by whitespace. The first column is the item of input data to
be replaced and the second column is the replacement. Multiple items
from column 1 may have the same value in column 2.

By default, map files are saved in the output directory as
anonmap_Xmap.txt, where X is replaced with a kind indicator (i, p, n,
h). The prefix option is used to relocate these outputs. They cannot be
suppressed.

In the special case of host names and host lists, name fragment
substitutions are supported. Any appearance of a host list, such as
gw[1,3-5] is expanded to single hostnames. Each host name is split at
"-", and each fragment is checked for a replacement from the hmap file.
Any fragment not found in the hmap has right-side digits 0-9 stripped
and mapping the remainder is again attempted; if successful, the
stripped number is appended to the result, otherwise an error occurs.
The fragments are rejoined with "-". When all hosts in the appearance
have been rewritten, the host list is collapsed before output.

The special host map element 'netdomains' is used to remove fully
qualified domain suffixes. It is a comma separated list of suffixes, and
order matters (subdomains should come before their root if both appear).
Suffix removal occurs before substitution.

NOTES
=====

There is no column delete option; use :ref:`cut(1) <cut>` to remove entire columns.

To ensure map consistency across multiple runs, use the map outputs as
the map inputs to the second and subsequent runs.

EXAMPLES
========

In bash:

::

   colargs=$(ldms-csv-anonymize \
     --gen-args=host:ProducerName,int:uid,name:username,jobid.HEADER)

   ldms-csv-anonymize $colargs \
    --out-dir=/tmp \
    --save-maps=anonjob_ \
    --hmap=/home/anonjob_hmap.txt \
    --input=/home/jobid.csv

and in a host map file:

::

   #anonymize-csv-map host
   netdomains .ca.sandia.gov,.sandia.gov
   compute node
   admin svc

will cause compute01 to be replaced with node01 and admin7 to be
replaced with svc7. The .sandia.gov and .ca.sandia.gov domains will be
stripped.

BUGS
====

There is no pipeline filtering mode.

SEE ALSO
========

:ref:`cut(1) <cut>`

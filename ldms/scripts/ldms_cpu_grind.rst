.. _ldms_cpu_grind:

==============
ldms_cpu_grind
==============

--------------------
Execute the program
--------------------

:Date:   21 Aug 2020
:Manual section: 8
:Manual group: LDMS tests

SYNOPSIS
========

ldms_cpu_grind -h

ldms_cpu_grind [options]

DESCRIPTION
===========

ldms_cpu_grind provides a single-threaded cpu load.
Options control the duration, size of data, and type of arithmetic.

OPTION LIST
===========

-h
   |
   | List help message.

-n,--n <int>
   |
   | Set the value of vector size N

-m,--method <name>
   |
   | Pick method to use.
     name is one of:
        add: sum the rows of a
        multiply: compute out = a dot in
        scale: compute a \*= coef
        exp: compute a = exp( (a + coef)/2 ) as scalars
        cos: compute a = cos(a) as scalars

-p,--pid <pathname>
   |
   | Write process id to file pathname.
     No redundancy checking included.

-r,--repeat <int>
   |
   |  Set the repetition count, or -1 for infinite loop.

-s,--seed <int>
   |
   |  Set the repetition count.

-c,--coefficient <double>
   |
   | Set the coefficient.

NOTES
=====

This test emulates cpu-heavy applications which may delay execution
of ldmsd threads.

When the time to execute the test exceed 1-10 milliseconds,
operating system noise can severely degrade bandwidth results reported
by this utility; this is not a rigorous bandwidth benchmarking utility.

This program is a tool for LDMS scale testing. It does not depend on
LDMS libraries.


SEE ALSO
========

:ref:`ldms_ldms-static-test(8) <ldms-static-test.sh>`, :ref:`ldms_pll-ldms-static-test(8) <pll-ldms-static-test.sh>`

.. _ldmsd_setgroup:

==============
ldmsd_setgroup
==============

------------------------------------------------------------
Explanation, configuration, and commands for ldmsd set group
------------------------------------------------------------

:Date:   5 Jul 2018
:Manual section: 7
:Manual group: LDMSD


SYNOPSIS
========

-  **setgroup_add**

        name=\ *GROUP_NAME* [producer=\ *PRODUCER*] [interval=\ *USEC*]
        [offset=\ *USEC*]

-  **setgroup_mod**

        name=\ *GROUP_NAME* [interval=\ *USEC*] [offset=\ *USEC*]

-  **setgroup_del*

        name=\ *GROUP_NAME*

-  **setgroup_ins**

        name=\ *GROUP_NAME* instance=\ *COMMA_SEPARATED_LIST_OF_INSTANCES*

-  **setgroup_rm**

        name=\ *GROUP_NAME* instance=\ *COMMA_SEPARATED_LIST_OF_INSTANCES*

DESCRIPTION
===========

An **ldmsd setgroup** (referred to as **setgroup** for short) is an
**ldms_set** with special information for LDMS daemon (**ldmsd**). The
setgroup information contains a list of other sets so that the LDMSD
**updtr** can update all the sets in the collection at once
(iteratively). This will help administrators in configuration, and help
sampler plugin developer to manage their collection of sets. For an
example usage of \`ldmsd_group_*\` APIs, please see **grptest.c**, and
\`ldmsd_group_*()\` declarations (with doxygen doc) in **ldmsd.h**. In
this manual page, we will focus on LDMSD commands that manage the
setgroup from the configuration side. The description for each command
and its parameters is as follows.

**setgroup_add** adds (creates) a new setgroup. The following list
describes the command parameters:

   -  **name=GROUP_NAME**

        The name of the setgroup.

   -  **[producer=**\ *PRODUCER*\ **]**

        (Optional) The producer name of the setgroup. If not set, the name
        of the LDMSD (the **-n** option) is used.

   -  **[interval=**\ *USEC*\ **]**

        (Optional) The micro-second update interval hint.

   -  **[offset=**\ *USEC*\ **]**

        (Optional) The micro-second update offset hint.

**setgroup_mod** modifies (mutable) attributes of the setgroup. The list
of parameters is as follows:

   -  **name=GROUP_NAME**

        The name of the setgroup.

   -  **[interval=**\ *USEC*\ **]**

        (Optional) The micro-second update interval hint.

   -  **[offset=**\ *USEC*\ **]**

        (Optional) The micro-second update offset hint.

**setgroup_ins** inserts a list of set instances into the setgroup.

   -  **name=GROUP_NAME**

        The name of the setgroup.

   -  **[instance=**\ *COMMA_SEPARATED_LIST_OF_INSTANCES*\ **]**

        A comma-separated list of set instances.

**setgroup_rm** removes a list of set instances from the setgroup.

   -  **name=GROUP_NAME**

        The name of the setgroup.

   -  **[instance=**\ *COMMA_SEPARATED_LIST_OF_INSTANCES*\ **]**

        A comma-separated list of set instances.

**setgroup_del** deletes the setgroup.

   -  **name=GROUP_NAME**

        The name of the setgroup.

EXAMPLE
=======

In this example, we will have 2 **ldmsd**'s, namely **sampler** and
**aggregator** for the sampler daemon and the aggregator daemon
respectively. The sampler will have \`meminfo`, \`set_0`, \`set_1`,
\`set_2`, \`set_3\` as its regular sets. \`thegroup\` will be the
setgroup created in the sampler that contains \`meminfo\` and \`set_0`.
The aggregator will be setup to update only \`thegroup`.

::

   ### sampler.conf
   # It is OK to add the group first, please also not that our group has no
   # update hint so that the updater in the aggregator can control its update
   # interval.
   setgroup_add name=thegroup

   # Insert meminfo and set_0 into the group
   setgroup_ins name=thegroup instance=meminfo,set_0

   # test_sampler will generate a bunch of sets, with this config it will create
   # set_0, set_1, set_2, set_3
   load name=test_sampler
   config name=test_sampler producer=sampler \
          action=default \
          base=set \
          num_sets=4 \
          push=0
   start name=test_sampler interval=1000000 offset=0
   # meminfo
   load name=meminfo
   config name=meminfo producer=sampler \
          instance=meminfo
   start name=meminfo interval=1000000 offset=0
   ### END OF sampler.conf

   ### aggregator.conf
   # Normal producer setup
   prdcr_add name=prdcr host=localhost port=10001 xprt=sock \
             interval=1000000 \
             type=active
   prdcr_start name=prdcr
   # Setup the `grp_updtr` so that it only updates `thegroup`.
   updtr_add name=grp_updtr interval=1000000 offset=500000
   updtr_match_add name=grp_updtr regex=thegroup
   updtr_prdcr_add name=grp_updtr regex=prdcr
   updtr_start name=grp_updtr
   ### END OF sampler.conf

The daemons can be started with the following commands:

::

   # For sampler, foreground start
   $ ldmsd -F -c sampler.conf -x sock:10001
   # For aggregator, foreground start
   $ ldmsd -F -c aggregator.conf -x sock:10000

When listing the sets on the aggregator with **-v** option, you'll see
that only \`meminfo\` and \`set_0\` are recent. \`thegroup\` is only
updated when its information changed. The rest of the sets only been
looked-up, but not updated.

::

   $ ldms_ls -x sock -p 10000 -v | grep update
   thegroup: consistent, last update: Thu Jul 05 16:22:08 2018 [303411us]
   set_3: inconsistent, last update: Wed Dec 31 18:00:00 1969 [0us]
   set_2: inconsistent, last update: Wed Dec 31 18:00:00 1969 [0us]
   set_1: inconsistent, last update: Wed Dec 31 18:00:00 1969 [0us]
   set_0: consistent, last update: Thu Jul 05 16:36:30 2018 [1793us]
   meminfo: consistent, last update: Thu Jul 05 16:36:31 2018 [1946us]

While when listing the sets on the sampler, we will see all of them
being updated (except \`thegroup`).

::

   thegroup: consistent, last update: Thu Jul 05 16:22:08 2018 [303411us]
   set_3: consistent, last update: Thu Jul 05 16:39:52 2018 [1915us]
   set_2: consistent, last update: Thu Jul 05 16:39:52 2018 [1916us]
   set_1: consistent, last update: Thu Jul 05 16:39:53 2018 [1948us]
   set_0: consistent, last update: Thu Jul 05 16:39:53 2018 [1948us]
   meminfo: consistent, last update: Thu Jul 05 16:39:53 2018 [2022us]

**Removing/inserting** instances from/into the group can also be done
interactively via **ldmsd_controller**. If we do the following on the
**sampler**:

::

   $ ldmsd_controller --port 10001
   Welcome to the LDMSD control processor
   sock:localhost:10001> setgroup_rm name=thegroup instance=set_0
   sock:localhost:10001> setgroup_ins name=thegroup instance=set_3

\`set_0\` will be removed from \`thegroup`, and \`set_3\` will be added
into \`thegroup`. Listing the sets on the **aggregator** will see that
\`set_0\` stopped being updated, and \`set_3\` becomes recent.

::

   thegroup: consistent, last update: Thu Jul 05 16:42:12 2018 [378918us]
   set_3: consistent, last update: Thu Jul 05 16:42:14 2018 [2070us]
   set_2: inconsistent, last update: Wed Dec 31 18:00:00 1969 [0us]
   set_1: inconsistent, last update: Wed Dec 31 18:00:00 1969 [0us]
   set_0: consistent, last update: Thu Jul 05 16:41:25 2018 [1116us]
   meminfo: consistent, last update: Thu Jul 05 16:42:15 2018 [1223us]

The **members** of the group can be **listed** by the following:

::

   $ ldms_ls -x sock -p 10000 -v thegroup
   thegroup: consistent, last update: Thu Jul 05 16:42:12 2018 [378918us]
     APPLICATION SET INFORMATION ------
            grp_member: set_3 : -
            grp_member: meminfo : -
                ldmsd_grp_gn : 8
     METADATA --------
       Producer Name : a:10001
       Instance Name : thegroup
         Schema Name : ldmsd_grp_schema
                Size : 184
        Metric Count : 1
                  GN : 1
                User : :ref:`root(0) <root>`
               Group : :ref:`root(0) <root>`
         Permissions : -rwxrwxrwx
     DATA ------------
           Timestamp : Thu Jul 05 16:42:12 2018 [378918us]
            Duration : [0.000017s]
          Consistent : TRUE
                Size : 64
                  GN : 8
     -----------------

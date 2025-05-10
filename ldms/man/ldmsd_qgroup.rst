.. _ldmsd_qgroup:

============
ldmsd_qgroup
============

-----------------------------
Quota Group Feature in LDMSD
-----------------------------

:Date:   11 Sep 2024
:Manual section: 7
:Manual group: LDMSD

SYNOPSIS
========

-  **qgroup_config**

        [quota=\ *BYTES*] [ask_interval=\ *TIME*] [ask_amount=\ *BYTES*]
        [ask_mark=\ *BYTES*] [reset_interval=\ *TIME*]

-  **qgroup_member_add**

        xprt=\ *XPRT* host=\ *HOST* [port=\ *PORT*] [auth=\ *AUTH*]

-  **qgroup_member_del**

        host=\ *HOST* [port=\ *PORT*]

-  **qgroup_start**

-  **qgroup_stop**

-  **qgroup_info**

DESCRIPTION
===========

Quota Group (**qgroup**) is a feature in LDMS to restrict the flow of
LDMS Stream data through the group in a time interval to prevent the
LDMS stream from dominating the network usage and significantly
affecting the running application. **qgroup** consists of multiple
**ldmsd** processes that can donate unused **quota** to other member
processes that need it, so that we can appropriately utilize the allowed
network bandwidth.

Please see **QGROUP MECHANISM** section on how **qgroup** works and the
meaning of the configuration parameters. The **QGROUP CONFIGURATION**
section contains ldmsd commands related to **qgroup** manipulation.

QGROUP MECHANISM
================

Each ldmsd participating in **qgroup** has to be configured such in add
all other members into its **qgroup** member list using
**qgroup_member_add** commands. This is so that each member can
communicate to each other regarding quota requesting/donating. In
addition, the ldmsd in **qgroup** should have per-connection **quota**
set (in **listen** and **prdcr_add** command) to limit peer's
outstanding stream data holding in the member processes. If this
per-connection **quota** is not set (i.e. being UNLIMTED), the peers can
send unlimited amount of data to the processes in the **qgroup**.

Let's setup a simple **ldmsd**'s cluster to explain the **qgroup**
mechanism. There are 6 daemons: 4 samplers (samp.[1-4]) an 2 L1
aggregators (L1.[1-2]). L1.1 connects (**prdcr_add**) samp.1 and samp.2.
L1.2 connects to samp.3 and samp.4. Both L1.1 and L1.2 are in **qgroup**
(i.e. they **qgroup_member_add** each other). The **prdcr_add.quota** of
L1.1 and L1.2 is set to 6, and they are tracked by the samplers. The
square filled boxes (■) represent available **quota** for **publish**
operation on the samplers. The filled diamonds (◆) on L1.1 and L1.2
represent available "return" **quota**. Normally, when an L1 daemon
finishes processing the stream data, it returns the quota to the
corresponding peer right away. With **qgroup**, The L1 damon will take
the return quota from the available return quota (◆) before returning
the quota back to the corresponding peer. If there is not enough
available return quota, the L1 daemon delays the return (in a queue)
until there is enough available return quota.

::

 ┌──────┐
 │samp.1│
 │■■■■■■├──────┐
 └──────┘      │     ┌────────────────┐
               └─────┤L1.1   ▽        ├───┄
 ┌──────┐      ┌─────┤◆◆◆◆◆◆◆◆◆◆◆◆◆◆◆◆│
 │samp.2│      │     └───────┬────────┘
 │■■■■■■├──────┘             │
 └──────┘                    │
                             │
 ┌──────┐                    │
 │samp.3│                    │
 │■■■■■■├──────┐             │
 └──────┘      │     ┌───────┴────────┐
               └─────┤L1.2   ▽        ├───┄
 ┌──────┐      ┌─────┤◆◆◆◆◆◆◆◆◆◆◆◆◆◆◆◆│
 │samp.4│      │     └────────────────┘
 │■■■■■■├──────┘
 └──────┘


As things progress, L1's available return quota (referred to as
**qgroup.quota** for distinction) will eventually run low and won't be
able to return the quota back to any peer anymore. When this happens the
peer quota (for publishing) eventually runs out as seen below.

::

 ┌──────┐
 │samp.1│
 │■■□□□□├──────┐
 └──────┘      │     ┌────────────────┐
               └─────┤L1.1   ▽        ├───┄
 ┌──────┐      ┌─────┤◆◇◇◇◇◇◇◇◇◇◇◇◇◇◇◇│
 │samp.2│      │     └───────┬────────┘
 │□□□□□□├──────┘             │
 └──────┘                    │
                             │
 ┌──────┐                    │
 │samp.3│                    │
 │■■■■■■├──────┐             │
 └──────┘      │     ┌───────┴────────┐
               └─────┤L1.2   ▽        ├───┄
 ┌──────┐      ┌─────┤◆◆◆◆◆◆◆◆◆◆◆◆◆◆◆◆│
 │samp.4│      │     └────────────────┘
 │■■■■■■├──────┘
 └──────┘

When the **qgroup.quota** is low, i.e. **qgroup.quota** ◆ lower than the
threshold **ask_mark** (denoted as ▽ in the figure), the daemon asks for
a donation from all other members. To prevent from asking too
frequently, the **qgroup** members ask other members in
**ask_interval**. The amount to ask for is set by **ask_amount**
parameter. The members who are asked for the donation may not donate
fully or may not donate at all, depending on the members'
**qgroup.quota** level.

::

 ┌──────┐
 │samp.1│
 │■■□□□□├──────┐
 └──────┘      │     ┌────────────────┐
               └─────┤L1.1   ▽        ├───┄
 ┌──────┐      ┌─────┤◆◆◆◆◆◆◆◆◇◇◇◇◇◇◇◇│
 │samp.2│      │     └───────┬────────┘
 │□□□□□□├──────┘             │
 └──────┘                    │
                             │
 ┌──────┐                    │
 │samp.3│                    │
 │■■■■■■├──────┐             │
 └──────┘      │     ┌───────┴────────┐
               └─────┤L1.2   ▽        ├───┄
 ┌──────┐      ┌─────┤◆◆◆◆◆◆◆◆◆◇◇◇◇◇◇◇│
 │samp.4│      │     └────────────────┘
 │■■■■■■├──────┘
 └──────┘

Asking/donating **qgroup.quota** allows the busy members to continue
working while reducing the unused **qgroup.quota** in the less busy
members in the **qgroup**. The **qgroup.quota** in all members will
eventually run out, and no stream data will be able to go through the
group -- restricting LDMS stream network usage.

The **qgroup.quota** of each member in the **qgroup** resets to its
original value in **reset_interval** time interval, and the quota
returning process continues.

The maxmum amount of stream data that go through the group per unit time
can be calculated by:

::

        N \ qgroup.quota
        ────────────────
        reset_interval

QGROUP COMMANDS
===============

-  **qgoup_config** [quota=\ *BYTES*] [ask_interval=\ *TIME*] [ask_amount=\ *BYTES*]
   [ask_mark=\ *BYTES*] [reset_interval=\ *TIME*]

..

   Configure the specified qgroup parameters. The parameters not
   specifying to the command will be left untouched.

   **[quota=**\ *BYTES*\ **]**
      The amount of our quota (bytes). The *BYTES* can be expressed with
      quantifiers, e.g. "1k" for 1024 bytes. The supported quantifiers
      are "b" (bytes), "k" (kilobytes), "m" (megabytes), "g" (gigabytes)
      and "t" (terabytes).

   **[ask_interval=**\ *TIME*\ **]**
      The time interval to ask the members when our quota is low. The
      *TIME* can be expressed with units, e.g. "1s", but will be treated
      as microseconds if no units is specified. The supported units are
      "us" (microseconds), "ms" (milliseconds), "s" (seconds), "m"
      (minutes), "h" (hours), and "d" (days).

   **[ask_amount=**\ *BYTES*\ **]**
      The amount of quota to ask from our members. The *BYTES* can be
      expressed with quantifiers, e.g. "1k" for 1024 bytes. The
      supported quantifiers are "b" (bytes), "k" (kilobytes), "m"
      (megabytes), "g" (gigabytes) and "t" (terabytes).

   **[ask_mark=**\ *BYTES*\ **]**
      The amount of quota to determine as 'low', to start asking quota
      from other members. The *BYTES* can be expressed with quantifiers,
      e.g. "1k" for 1024 bytes. The supported quantifiers are "b"
      (bytes), "k" (kilobytes), "m" (megabytes), "g" (gigabytes) and "t"
      (terabytes).

   **[reset_interval=**\ *TIME*\ **]**
      The time interval to reset our quota to its original value. The
      *TIME* can be expressed with units, e.g. "1s", but will be treated
      as microseconds if no units is specified. The supported units are
      "us" (microseconds), "ms" (milliseconds), "s" (seconds), "m"
      (minutes), "h" (hours), and "d" (days).

-  **qgroup_member_add** xprt=\ *XPRT* host=\ *HOST* [port=\ *PORT*] [auth=\ *AUTH*]

..

   Add a member into the process' qgroup member list.

   **xprt=**\ *XPRT*
      The transport type of the connection (e.g. "sock").

   **host=**\ *HOST*
      The hostname or IP address of the member.

   **[port=**\ *PORT*\ **]**
      The port of the member (default: 411).

   **[auth=**\ *AUTH_REF*\ **]**
      The reference to the authentication domain (the **name** in
      **auth_add** command) to be used in this connection If not
      specified, the default authentication domain of the daemon is
      used.

-  **qgroup_member_del** host=\ *HOST* [port=\ *PORT*]

..

   Delete a member from the list.

   **host**\ *HOST*
      The hostname or IP address of the member.

   **[port**\ *PORT*\ **]**
      The port of the member (default: 411).

-  **qgroup_start**

..

   Start the qgroup service.

-  **qgroup_stop**

..

   Stop the qgroup service.

-  **qgroup_info**

..

   Print the qgroup information (e.g. current quota value, parameter
   values, member connection states, etc).

EXAMPLE
=======

::

	qgroup_config quota=1M ask_interval=200ms ask_mark=200K ask_amount=200K reset_interval=1s

	qgroup_member_add host=node-2 port=411 xprt=sock auth=munge

	qgroup_member_add host=node-3 port=411 xprt=sock auth=munge

	qgroup_start

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`

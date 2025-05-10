.. _ldms_build_install:

==================
ldms_build_install
==================

----------------------------------------------
Instructions for building and installing ldms
----------------------------------------------

:Date:   22 Dec 2016
:Manual section: 8
:Manual group: LDMS

INTRODUCTION
============

OVIS is a modular system for HPC data collection, transport, storage,
analysis, visualization, and response. The Lightweight Distributed
Metric Service (LDMS) is the OVIS data collection and transport system.
LDMS provides capabilities for lightweight run-time collection of
high-fidelity data. Data can be accessed on-node or transported off
node. Additionally, LDMS can store data in a variety of storage options.

This entire source encompasses a number of the modular components of
OVIS. The top level subdirectory ldms contains the ldms source. This
document covers building only the ldms component from the top level
directory.

DESCRIPTION
===========

This document covers building only the ldms component from the top level
directory.

ldms is built via the following steps:

   ::

      build prerequisties
      cd top_level_directory
      mkdir build
      cd build
      make
      make install


      This document describes the steps involved in building the prerequisties and in doing the configure.
      A description of the arguments for configure can be found by invoking

      ./configure --help

      at BOTH the top level and in the ldms subdirectory.

PREREQUISTES:
=============

-  libevent-2.0 is a requirement. It can be built from source obtained
   from libevent.org or it can be installed from rpm or similar on your
   system via a utility like yum. If you do the latter, then you need to
   install both the libevent and libevent-devel packages.

-  If you intend to use the aries_mmr sampler, then you will need to
   install Cray's gpcd library. More information on this can be found in
   the Plugin.aries_mmr man page. (This is the recommended method for
   getting HSN metrics for the Aries).

-  If you intend to use the hsn metrics in the cray_aries_r_sampler or
   the cray_gemini_r_sampler, you will need to configure gpcdr. More
   information on this can be found in the Plugin.cray_sampler_variants
   man page. (This is the recommended method for the Gemini).

-  Use the gnu compiler for building ldms. (This may necessitate a
   module change on some platforms).

The remaining instructions will include paths to where the headers and
libraries of these prerequisties are installed.

CONFIGURATION OPTIONS
=====================

There are configuration options at the top level, in ldms, and in the
ovis_ldms support directories. This section is thus split into these
three sections, however the configuration arguments are all combined as
arguments to the top level configure. The list of configuration options
give here is not comprehensive, rather it refers to the most common
arguments.

TOP LEVEL OPTIONS
-----------------

A number of top level "enable|disable-feature" options exist. The
defaults are chosen for a generic linux build to work by default.

**--enable|disable-rpath**
   |
   | Disable this. Do not hardcode runtime library paths.

**--enable|disable-ldms**
   |
   | Enable this. Default enabled.

**--enable|disable-sos**
   |
   | Used to enable or disable sos. Enable only if you are going to use
     the store_sos plugin. Default disable.

**--enable|disable-ocm|baler|me|komondor**
   |
   | Disable all of these. All default disabled.

OVIS_LIB LEVEL OPTIONS
----------------------

A number of top level "enable|disable-feature" options exist. The
defaults are chosen for a generic linux build to work by default.

**--enable|disable-auth**
   |
   | Enables or disables authentication. Default enabled.

**--enable|disable-sock**
   |
   | Enables or disables the sock transport. Default enabled.

**--enable|disable-rdma**
   |
   | Enables or disables the rdma transport. Default disabled

**--enable|disable-ugni**
   |
   | Enables or disables the ugni transport. The is cray-specific for
     rdma over gemini or aries. Default disabled.

LDMS LEVEL OPTIONS
------------------

A number of "enable|disable-feature options" exist. In addition a number
of "with" options exist to specify paths to files/libraries/etc. The
defaults are chosen for a generic linux build to work by default.

General Options
---------------

**--enable|disable-ovis_auth**
   |
   | If --enable, then disable/enable authentication. Default enabled.

**--enable|disable-python**
   |
   | Enable the ldms python api and the configuration tools that depend
     on the API. Default: enabled if python and cython detected.
     **--enable|disable-readline**
   | Enable or disable the readline module. It is necessary to enable if
     you want to use the configuration tools interactively; if you are
     going to use a script interface to the configuration tools (usual
     method), then this can be disabled.

**--with-libevent**\ *[=path]*
   |
   | Specify libevent path [default=/usr]

Generic Sampler Options
-----------------------

**--enable|disable-meminfo|procinterrupts|procnfs|procnetdev|vmstat**
   |
   | Enable or disable generic linux samplers for data in /proc. Default
     enabled.

**--enable|disable-lustre**
   |
   | Enable or disable the lustre module. Default enabled.

Cray-specific Sampler Options
-----------------------------

**--enable|disable-kgnilnd**
   |
   | Enable the kgnilnd sampler. Default disabled.

**--enable|disable-cray_system_sampler**
   |
   | Enable or disable the cray_system_sampler module. Default disabled.
     If you enable this, then consider the following options:

   **--enable-gemini-gpcdr**
      |
      | Enable the gemini-gpcdr version of the cray_system_sampler.
        Default disabled. Both the gemini and aries versions can be
        built simultaneously.

   **--enable-aries-gpcdr**
      |
      | Enable the aries-gpcdr version of the cray_system_sampler.
        Default disabled. For the Aries, we recommended getting the HSN
        metrics via aries-mmr, instead of the aries-gpcdr sampler. Still
        build the aries-gpcdr sampler, but run it without the HSN part
        of the metric collection. Both the gemini and aries versions can
        be built simultaneously.

   **--enable-cray-nvidia**\ OR\ **--with-cray-nvidia-inc**\ [=path]
      |
      | For gemini systems with gpus, Enable the cray-nvidia metric
        sampling in the cray_gemini_r_sampler. You need not specify
        --enable-cray-nvidia if you are instead specifying the path to
        the include file via --with-cray-nvidia-inc.

   **--enable|disable-lustre**
      |
      | Enable or disable the lustre module for use in the
        cray_system_sampler. Default enabled.

   **--with-rca**\ *[=path]*
      |
      | Specify the path to the rca includes via --with-rca
        [default=/usr].

   **--with-krca**\ *[=path]*
      |
      | Specify the path to the krca includes via --with-krca
        [default=/usr].

   **--with-cray-hss-devel**\ *[=path]*
      |
      | Specify the path to the hss-devel includes via
        --with-cray-hss-devel [default=/usr].

**--enable|disable-aries-mmr**
   |
   | Enable or disable the aries-mmr module. Default disabled. If you
     enable this, then consider the following options:

   **--with-aries-libgpcd**\ *LIBDIR,INCDIR*
      |
      | Locations of gpcd library and headers for aries_mmr sampler.
        E.g. --with-aries-libgpcd=/special/libs,/private/headerdir

Store Options
-------------

**--enable|disable-csv**
   |
   | Enable the csv stores (store_csv and store_function_csv). Default
     enable. **--enable|disable-sos**
   | Enable or disable the sos stores. Enable this only if you are going
     to use the store_sos plugin. Default disable.

INSTALL DIRECTORY SETUP
=======================

The build will go into prefix (/XXX/Build/build_ovis in the examples
section below).

-  bin - python-based utility commands, such as ldmsd_controller. Also
   test scripts.

-  include - subdurectories with header files

-  lib - libraries. At the top level are libraries for the ldms
   infrastructure (e.g., libldms.so, libzap.so, etc). There is a
   subdirectory, which will be called either ovis-ldms or ovis-lib which
   contains all the libraries for the plugins (samplers, such as
   libmeminfo.so; stores, such as libstore_csv.so; and transports, such
   as libzap_sock.so).

-  lib64 - python library

-  sbin - C-based utility commands, such as ldms_ls and ldmsd.

-  share - documentation, including man pages.

NOTES
=====

This document does not cover putting the install into a cray-system
image. Nor does it over setting up init scripts to run ldms as a system
service (for any type of linux platform).

EXAMPLES
========

configure.sh script for a Cray XC install with the cray-specific
samplers only:

::

   PREFIX=/XXX/Build/build_ovis
   LIBDIR=${PREFIX}/lib

   # add --enable-FEATURE here
   ENABLE="--enable-ugni --enable-ldms-python --enable-kgnilnd --enable-lustre --enable-aries_mmr --enable-cray_system_sampler --enable-aries-gpcdr"

   # add --disable-FEATURE here
   DISABLE="--disable-rpath --disable-readline --disable-mmap --disable-baler --disable-sos"

   # libevent2 prefix
   LIBEVENT_PREFIX=/XXX/Build/libevent-2.0_build

   WITH="--with-rca=/opt/cray/rca/default/ --with-krca=/opt/cray/krca/default --with-cray-hss-devel=/opt/cray-hss-devel/default/ --with-pkglibdir=ovis-ldms --with-aries-libgpcd=/XXX/Build/gpcd/lib/,/XXX/Build/gpcd/include/"


   if [ -n "$LIBEVENT_PREFIX" ]; then
       WITH="$WITH --with-libevent=$LIBEVENT_PREFIX"
   fi

   CFLAGS='-g -O0'

SEE ALSO
========

:ref:`ldms_authentication(8) <ldms_authentication>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd(8) <ldmsd>`,
:ref:`cray_sampler_variants(7) <cray_sampler_variants>`, :ref:`aries_mmr(7) <aries_mmr>`,
:ref:`store_csv(7) <store_csv>`, :ref:`store_function_csv(7) <store_function_csv>`

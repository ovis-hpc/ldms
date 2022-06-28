GEOPM LDMS SAMPLER PLUGIN
=========================

This directory contains the source, build scripts, and unit tests for
the GEOPM LDMS Sampler Plugin. When enabled, this sampler relies on the
interfaces provided by the GEOPM Service package to read signals. This package
is part of the open source [GEOPM](https://geopm.github.io) project. 

Build Requirements
------------------

The GEOPM LDMS sampler currently requires version 2.0 of the GEOPM
Service library (``libgeopmd.so``).  This library and the corresponding
headers used for compiling the sampler are available as RPM packages
for several Linux distributions or may be built from source.

### Installing RPMs

The GEOPM LDMS sampler **build** requirements are met with the
``libgeopmd0`` and ``geopm-service-devel`` packages. Only the
``libgeopmd0`` package is required when **running** the GEOPM LDMS
sampler. The required version of these packages can be obtained here:
- [libgeopmd0](https://software.opensuse.org/download.html?project=home%3Ageopm%3Arelease-v2.0-candidate&package=libgeopmd0)
- [geopm-service-devel](https://software.opensuse.org/download.html?project=home%3Ageopm%3Arelease-v2.0-candidate&package=geopm-service-devel)

All requirements will be installed in standard locations.

For reference, the instructions for all GEOPM Service packages
(besides ``libgeopmd0`` and ``geopm-service-devel``) can be found
here: [Install Instructions](https://geopm.github.io/install.html)

### Building From Source

The user may optionally build the GEOPM Service package from source.
Please note that when building from source, libraries (e.g.
``libgeopmd.so``) are not installed automatically in standard locations,
so make sure to set ``LD_LIBRARY_PATH`` accordingly when building the
GEOPM LDMS sampler, if need be.


The bash script below shows an example source build that uses the
``v2.0.0+rc1`` release candidate:

    #!/bin/bash
    # Build GEOPM libraries
    GEOPM_URL="https://github.com/geopm/geopm/releases/download"
    GEOPM_RELEASE="/v2.0.0%2Brc1/geopm-service-2.0.0.rc1.tar.gz"
    wget ${GEOPM_URL}${GEOPM_RELEASE}
    tar xvf geopm-service-2.0.0.rc1.tar.gz
    cd geopm-service-2.0.0~rc1/
    GEOPM_PREFIX=$HOME/build/geopm
    # Use configure --help for details on enabling optional accelerator support 
    ./configure --prefix=${GEOPM_PREFIX} --libdir=${GEOPM_PREFIX}/lib64
    make
    make install
    
For reference, the instructions for building different components
of the GEOPM project can be found here: [Source Build Instructions](https://geopm.github.io/devel.html#developer-build-process)


OVIS Build
----------

To enable the sampler provide the ``--with-geopm`` option to the configure 
script while building OVIS from source. Please refer to the OVIS documentation 
for general guidance on building the OVIS/LDMS codebase. 


    #!/bin/bash
    # OVIS_SOURCE is the OVIS source directory
    cd $OVIS_SOURCE
    ./configure --with-geopm=${GEOPM_PREFIX}



Using the GEOPM LDMS Sampler
------------------------------

### Configuring the Sampler

The GEOPM LDMS sampler can be configured with the same config parameters 
as other LDMS samplers (e.g., ``name``, ``producer``, ``component_Id``). 
In addition to these paramers, the sampler must be configured with the 
option - ``geopm_request_path=<path-to-file>``. This parameter points to the 
absolute path of the ASCII file containing the list of signals that the user would 
like to have monitored by the sampler. The exact format for listing these signals is 
documented in the GEOPM LDMS sampler man page.


Here's an example of a file containing the list of signals:

    $> cat geopm_sampler_signal.lst
    CPU_FREQUENCY_MAX board 0
    CPU_FREQUENCY_MIN board 0
    CPU_FREQUENCY_STEP board 0
    CPU_FREQUENCY_STICKER board 0
    TIME board 0
    ENERGY_PACKAGE board 0
    INSTRUCTIONS_RETIRED board 0
    POWER_DRAM board 0
    POWER_PACKAGE board 0
    POWER_PACKAGE_LIMIT board 0
    POWER_PACKAGE_MAX board 0
    POWER_PACKAGE_MIN board 0
    POWER_PACKAGE_TDP board 0
    TEMPERATURE_CORE board 0
    TEMPERATURE_PACKAGE board 0
    TIMESTAMP_COUNTER board 0


An example of the sampler configure script is shown below:
 
    load name=ldms_geopm_sampler
    config name=ldms_geopm_sampler producer=${HOSTNAME} instance=${HOSTNAME}/ldms_geopm_sampler component_id=${COMPONENT_ID} schema=ldms_geopm_sampler job_set=${HOSTNAME}/jobinfo geopm_request_path=${SIGNAL_PATH}/geopm_sampler_signal.lst
    start name=ldms_geopm_sampler interval=${SAMPLE_INTERVAL} offset=${SAMPLE_OFFSET}
    

Note the inclusion of the ``geopm_request_path`` parameter passed to the 
``config`` instruction. Also, note the name of the sampler - ``ldms_geopm_sampler`` 
passed to the ``name`` parameter for the ``load`` & ``start`` instructions. 


### Running the sampler

In order to run the GEOPM LDMS sampler, follow the same steps as you would 
for any other LDMS sampler.  Start the ``ldmsd`` daemon is running on 
the target node to be monitored. Example below:

    ldmsd -x sock:10444 -F -c <path-to-sampler-conf-script> -l ${TEST_PATH}/temp/demo_ldmsd_log 


For observing the progress of the sampler, you may choose to add the 
option ``-v DEBUG`` above. While the ``ldmsd`` daemon is running, the user 
may choose to query for a single instantaneous sample set comprising of 
recently monitored signals.  This can be achieved by using the existing 
commandline tool - ``ldms_ls`` available as part of the installation of 
the LDMS framework. An example is shown below:

     $> ldms_ls -h localhost -x sock -p 10444 -l -v

     Schema         Instance                 Flags  Msize  Dsize  Hsize  UID    GID    Perm       Update            Duration          Info
     -------------- ------------------------ ------ ------ ------ ------ ------ ------ ---------- ----------------- ----------------- --------
     ldms_geopm_sampler <hostname>/ldms_geopm_sampler    CL    1352    240      0   1024    100 -r--r----- 1656431193.051578          0.000323 "updt_hint_us"="1000000:50000"
     -------------- ------------------------ ------ ------ ------ ------ ------ ------ ---------- ----------------- ----------------- --------
     Total Sets: 1, Meta Data (kB): 1.35, Data (kB) 0.24, Memory (kB): 1.59
     
     =======================================================================
     
     <hostname>/ldms_geopm_sampler: consistent, last update: Tue Jun 28 08:46:33 2022 -0700 [51578us]
     M u64        component_id                               1
     D u64        job_id                                     0
     D u64        app_id                                     0
     D d64        CPU_FREQUENCY_MAX-board-0                  3700000000.000000
     D d64        CPU_FREQUENCY_MIN-board-0                  1000000000.000000
     D d64        CPU_FREQUENCY_STEP-board-0                 100000000.000000
     D d64        CPU_FREQUENCY_STICKER-board-0              2100000000.000000
     D d64        TIME-board-0                               6.899751
     D d64        ENERGY_PACKAGE-board-0                     334936.207092
     D d64        INSTRUCTIONS_RETIRED-board-0               131016700.000000
     D d64        POWER_DRAM-board-0                         0.900889
     D d64        POWER_PACKAGE-board-0                      25.469352
     D d64        POWER_PACKAGE_LIMIT-board-0                140.000000
     D d64        POWER_PACKAGE_MAX-board-0                  594.000000
     D d64        POWER_PACKAGE_MIN-board-0                  140.000000
     D d64        POWER_PACKAGE_TDP-board-0                  280.000000
     D d64        TEMPERATURE_CORE-board-0                   26.454545
     D d64        TEMPERATURE_PACKAGE-board-0                28.000000
     D d64        TIMESTAMP_COUNTER-board-0                  10913748924506.000000
     

GEOPM LDMS SAMPLER PLUGIN
=========================

This directory contains the source, build scripts, and unit tests for
the GEOPM LDMS Sampler Plugin.  When enabled, this plugin relies on the
interfaces provided by the GEOPM Service package to read signals.

[GEOPM](https://geopm.github.io)

Build Requirements
------------------

The GEOPM LDMS plugin currently requires version 2.0 of the GEOPM
Service library (``libgeopmd.so``).  This library and corresponding
headers used for compiling the plugin are available as RPM packages
for several Linux distributions or may be built from source.

### Installing RPMs

The GEOPM LDMS plugin **build** requirements are met with the
``libgeopmd0`` and ``geopm-service-devel`` packages. Only the
``libgeopmd0`` package is required when **running** the GEOPM LDMS
plugin. The required version of these packages can be obtained here:
- [libgeopmd0](https://software.opensuse.org/download.html?project=home%3Ageopm%3Arelease-v2.0-candidate&package=libgeopmd0)
- [geopm-service-devel](https://software.opensuse.org/download.html?project=home%3Ageopm%3Arelease-v2.0-candidate&package=geopm-service-devel)

All requirements will be installed in standard locations.

For reference, the instructions for all GEOPM Service packages
(besides ``libgeopmd0`` and ``geopm-service-devel``) can be found
here:

[Install Instructions](https://geopm.github.io/install.html)

### Building From Source

The user may optionally build the GEOPM Service package from source.
Please note that when building from source, libraries (e.g.
libgeopmd.so) are not installed automatically in standard locations,
so make sure to set ``LD_LIBRARY_PATH`` accordingly when building the
GEOPM LDMS plugin.

[Source Build Instructions](https://geopm.github.io/devel.html#developer-build-process)

The bash script below shows an example source build that uses the
``v2.0.0+rc1`` release candidate:

    #!/bin/bash
    GEOPM_URL="https://github.com/geopm/geopm/releases/download"
    GEOPM_RELEASE="/v2.0.0%2Brc1/geopm-service-2.0.0.rc1.tar.gz"
    wget ${GEOPM_URL}${GEOPM_RELEASE}
    tar xvf geopm-service-2.0.0.rc1.tar.gz
    cd geopm-service-2.0.0~rc1/
    GEOPM_PREFIX=$HOME/build/geopm
    ./configure --prefix=${GEOPM_PREFIX} --libdir=${GEOPM_PREFIX}/lib64
    make
    make install
    # OVIS_SOURCE is the OVIS source directory
    cd $OVIS_SOURCE
    ./configure --with-geopm=${GEOPM_PREFIX}

NOTE: if you would like the LDMS sampler to monitor accelerator
telemetry, please consult the configure script help
(``./configure --help``) for available options.

OVIS Build
----------

To enable the plugin provide the ``--with-geopm`` option to the OVIS
configure script.

LDMS Plugin Option
------------------

The GEOPM LDMS plugin must be configured with the option
``geopm_request_path=<value>``.  This provides the path to the signal
request file.  The format for this file is documented in the GEOPM
LDMS plugin man page.

GEOPM LDMS PLUGIN
=================

This directory contains the source, build scripts and unit tests for
the GEOPM LDMS Plugin.  When enabled, this plugin can read signals
using the GEOPM PlatformIO interface.

[GEOPM](https://geopm.github.io)


Build Requirements
------------------

This build currently supports compiling against the GEOPM 2.0 library
``libgeopmd.so``.  This library and the headers used for compiling the
GEOPM LDMS sampler are available as RPM packages for several Linux
distributions.

[Install Instructions](https://geopm.github.io/install.html)

The GEOPM LDMS Plugin build requirements are met with the
``libgeopmd0`` and ``geopm-service-devel`` packages.  Only
the ``libgeopmd0`` package is a runtime requirement.

Note that the GEOPM HPC Runtime is **not** required to enable the
GEOPM LDMS Plugin, and only the GEOPM Service dependencies are
required.

[GEOPM Service](https://geopm.github.io/service.html)

The user may optionally build the GEOPM Service package from source.

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


OVIS Build
----------

To enable the plugin provide the ``--with-geopm`` option to the OVIS
configure script.


LDMS Plugin Option
------------------

The GEOPM LDMS Plugin must be configured with the option
``geopm_request_path=<value>``.  This provides the path to the signal
request file.  The format for this file is documented in the GEOPM
LDMS Plugin man page.

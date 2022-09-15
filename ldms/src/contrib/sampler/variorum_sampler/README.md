VARIORUM LDMS SAMPLER PLUGIN
=========================

This directory contains the source for the Variorum LDMS Sampler Plugin.
This sampler uses the JSON API from Variorum, a vendor-neutral library
that provides access to low-level power knobs regardless of system
architecture and implementation.

Build Requirements
------------------

The Variorum LDMS sampler currently requires version 0.6.0 or higher
of the Variorum library (``libvariorum.so``). This library must be built
from source. The sampler also requires jansson, which is a Variorum
dependency. If both libraries are installed in standard locations,
Autotools will locate them automatically. Otherwise, the user must use
the flags ``--with-libjansson-prefix=/jansson/path`` and ``--with-libvariorum-prefix=/variorum/path``
to point to the correct respective install locations when building LDMS.

### Installing Variorum

Variorum can be installed using the instructions here:
[Building Variorum](https://variorum.readthedocs.io/en/latest/BuildingVariorum.html)

The following bash commands show the steps for installation:

    # install hwloc and jansson dependencies
    sudo apt-get install libhwloc15 libhwloc-dev libjansson4 libjansson-dev

    git clone https://github.com/llnl/variorum

    cd variorum
    mkdir build install
    cd build

    cmake -DCMAKE_INSTALL_PREFIX=../install ../src
    make -j8
    make install

Note: the ``cmake`` command may require a ``host-config`` file, as
described here: [Host Config Files](https://variorum.readthedocs.io/en/latest/BuildingVariorum.html#host-config-files)

OVIS Build
----------

The sampler is enabled by default in the configure script
if Variorum and Jansson are both found. Please refer to the OVIS documentation
for general guidance on building the OVIS/LDMS codebase.

Using the Variorum LDMS Sampler
----------------------------

The sampler, when configured, automatically detects the number of sockets
on the host machine and then provides, for each socket, an LDMS record
containing power data. The sampler calls ``variorum_get_node_power_json``
internally, for which documentation can be found here:
[Variorum JSON-Support Functions](https://variorum.readthedocs.io/en/latest/api/json_support_functions.html)

For each socket, the values provided are: node power consumption in Watts (identical across sockets);
socket ID number; CPU power consumption in Watts;
GPU power consumption in Watts (aggregated across all GPUs on the socket, and
reported as -1 on unsupported platforms); and
memory power consumption in Watts.

More details on using the sampler can be found on the
[man page](Plugin_variorum_sampler.man).

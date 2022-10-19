[![status](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/ldms-test/weekly-report/master/status.json)](https://github.com/ldms-test/weekly-report/blob/master/summary.md)

# OVIS / LDMS

OVIS is a modular system for HPC data collection, transport, storage,
-log message exploration, and visualization as well as analysis.

LDMS is a low-overhead, low-latency framework for collecting, transfering, and storing
metric data on a large distributed computer system.

The framework includes:

* a public API with a reference implementation
* tools for collecting, aggregating, transporting, and storing metric values
* collectors for several common types of metrics
* Data transport over socket, RDMA (IB/iWarp/RoCE), and Cray Gemini as well as Aries

The API provides a way for vendors to expose system information in a uniform manner without
being required to provide source code for accessing the information (although we advise it be included)
which might reveal proprietary methods or information.

Metric information can be updated by a kernel module which runs only when
applications yield the processor and transported using RDMA-like operations, resulting in
minimal jitter during collection. LDMS has been run on 10,000 cores collecting
over 100,000 metric values per second with less than 0.2% overhead.

# Building the OVIS / LDMS source code

## Obtaining the source code

You may obtain the source code by obtaining an official release tarball, or by
cloning the ovis-hpc/ovis [Git](http://git-scm.com/) repository at github.

### Release tarballs

Official Release tarballs are available from the GitHub releases page:

  https://github.com/ovis-hpc/ovis/releases

The tarball is avialble in the "Assets" section of each release. Be sure to
download the tarball that has a name of the form "ovis-ldms-X.X.X.tar.gz".

The links that are named "Source code (zip)" and "Source code (tar.gz)" are
automatic GitHub links that we are unable to remove. They will be missing the
configure script, because they are raw source from git repository and
not the official release tarball distribution.

### Cloning the git repository

To clone the source code, go to https://github/com/ovis-hpc/ovis, and click
one the "Code" button. Or use the following command:

```git clone https://github.com/ovis-hpc/ovis.git -b OVIS-4```

## Build Dependencies

* autoconf (>=2.63)
* automake
* libtool
* make
* bison
* flex
* libreadline
* openssl development library (for OVIS, LDMS Authentication)
* libmunge (for Munge LDMS Authentication plugin)
* Python >= 3.6 and Cython >= 0.25 (for the LDMS Python API and ldmsd_controller)
* doxygen (for the OVIS documentation)

Some LDMS plug-ins have dependencies on additional libraries.

For cray-related LDMS sampler plug-in dependencies, please see the man page of the
plug-in in `ldms/man/`.

### RHEL7/CentOS7 dependencies

RHEL7/CentOS7 systems will require a the following packages at a minimum:

* autoconf
* automake
* libtool
* make
* bison
* flex
* openssl-devel

Additionally, the Python API and the ldmsd_controller command require Python and Cython.
One way to obtain those packages is from EPEL (install the epel-release package, and
then "yum update"). The packages from EPEL are:

* python3-devel
* python36-Cython

## Compling the code

If you are interested in storing LDMS data in SOS, then first
follow the instructions at https://github.com/ovis-hpc/sos to obtain,
build, and install SOS before proceding.

```sh
	cd <ovis source directory>
	sh autogen.sh
	./configure [--prefix=<installation prefix>] [other options]
	make
	make install
```

Run ```configure --help``` for a full list of configure options.

# Supported systems

* Ubuntu and friends
* CentOS and friends
* Cray XE6, Cray XK, Cray XC

# Unsupported features

The following LDMS sampler plugins are considered unsupported. Use are your own risk:
* perfevent sampler
* hweventpapi sampler
* switchx

## gnulib

Some m4 files come from the gnulib project. To update these files, first checkout
gnulib:

  git clone git://git.savannah.gnu.org/gnulib.git

There is no need to build or install the checked out code. The gnulib/gnulib-tool
program works directly from the checked out tree.

Next look at the comment at the top of the gnulib/Makefile.am file in the _ovis_
source tree. That comment will tell you the full gnulib-tool command to repeat
to install the latest versions of the currently selected components from gnulib.
Additional gnulib components can be added to the command line as more macros are
desired.

After running gnulib-tool, check in the resulting changes.

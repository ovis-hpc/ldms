====
LDMS
====

--------------------------------------
Lightweight Distributed Metric Service
--------------------------------------

|main status|

.. |main status| image:: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/ldms-test/weekly-report/master/status.json
   :target: https://github.com/ldms-test/weekly-report/blob/master/summary.md


|b44 status|

.. |b44 status| image:: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/ldms-test/weekly-report/b4.4/status.json
   :target: https://github.com/ldms-test/weekly-report/blob/b4.4/summary.md

For more information on installing and using LDMS: https://ovis-hpc.readthedocs.io/en/latest/

To join the LDMS Users Group: https://github.com/ovis-hpc/ovis-wiki/wiki/Mailing-Lists

Besides the Users Group, there have been three sub-workgroups: Best Practices,
Multi-tenancy, and Stream Security. To request access to the discussion
documents, create an Overleaf account and email Tom Tucker (tom@ogc.us) your
email address corresponding to your Overleaf account with the subject "LDMS-UG:
Request access to Workgroup Documents."

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

Building the OVIS / LDMS source code
====================================

Pre-built containers
--------------------

You may avoid building LDMS from scratch by leveraging containerized
deployments. Here's a collection of LDMS container images available for you to
pull and run. Each image offers a specific set of functionalities to suit your
needs. Please refer to the corresponding links for detailed information on each
image. They are currently built with OVIS-4.3.11.

- `ovishpc/ldms-samp <https://hub.docker.com/r/ovishpc/ldms-samp>`_:
  a small image for 'sampler' daemons meant to be deployed on compute nodes.
- `ovishpc/ldms-agg <https://hub.docker.com/r/ovishpc/ldms-agg>`_:
  an image for 'aggregator' daemons, which also includes various storage plugins.
- `ovishpc/ldms-storage <https://hub.docker.com/r/ovishpc/ldms-storage>`_:
  an image that contains storage technologies (e.g. SOS, Kafa).
- `ovishpc/ldms-web-svc <https://hub.docker.com/r/ovishpc/ldms-web-svc>`_:
  an image for the back-end (Django) that queries SOS data for a Grafana server.
- `ovishpc/ldms-grafana <https://hub.docker.com/r/ovishpc/ldms-grafana>`_:
  a Grafana image with 'DSOS' Grafana plugin that allows Grafana to get data
  from 'ovishpc/ldms-web-svc'.
- `ovishpc/ldms-dev <https://hub.docker.com/r/ovishpc/ldms-dev>`_:
  an image for LDMS code development and binary building.

NOTE: To quickly check the version of `ldmsd` in a container, issue the
following command:

.. code:: sh

   $ docker run --rm -it ovishpc/ldms-samp ldmsd -V


Obtaining ldms-dev container
----------------------------

You may build OVIS on your barebone computers. In which case, you can skip this
section. Alternatively, you may get
`ovishpc/ldms-dev <https://hub.docker.com/r/ovishpc/ldms-dev>`_ docker image from
docker hub which is an `ubuntu:22.04` container with required development
libraries. The following commands `pull` the image and `run` a container created
from it.

```sh
$ docker pull ovishpc/ldms-dev
$ docker run -it --name dev --hostname dev ovishpc/ldms-dev /bin/bash
root@dev $ # Now you're in 'dev' container
```

Please see `ovishpc/ldms-dev <https://hub.docker.com/r/ovishpc/ldms-dev>`_ for
more information about the container.


Docker Cheat Sheet
``````````````````
.. code:: sh

   $ docker ps # See contianers that are 'Up'
   $ docker ps -a  # See all containers (regardless of state )
   $ docker stop _NAME_ # Stop '_NAME_' container, this does NOT remove the container
   $ docker kill _NAME_ # Like `stop` but send SIGKILL with no graceful wait
   $ docker start _NAME_ # Start '_NAME_' container back up again
   $ docker rm _NAME_ # Remove the container '_NAME_'
   $ docker create -it --name _NAME_ --hostname _NAME_ _IMAGE_ _COMMAND_ _ARG_
     # Create a container '_NAME_' without starting it.
     # -i = interactive
     # -t = create TTY
     # --name _NAME_ to set _NAME_ for easy reference
     # --hostname _NAME_ to set the container hostname to _NAME_ to reduce
     #            confusion
     # _IMAGE_ the container image that the new container shall be created from
     # _COMMAND_ the command to run in the container (e.g. /bin/bash). This is
     #           equivalent to 'init' process to the container. When this process
     #           exited, the container stopped
     # _ARG_ the arguments to _COMMAND_
   $ docker create -it --name _NAME_ --hostname _NAME_ _IMAGE_ _COMMAND_ _ARG_
     # `create` + `start` in one go


Obtaining the source code
-------------------------

You may obtain the source code by obtaining an official release tarball, or by
cloning the ovis-hpc/ovis `Git <http://git-scm.com/>`_ repository at github.

Release tarballs
````````````````

Official Release tarballs are available from the GitHub releases page:

  https://github.com/ovis-hpc/ovis/releases

The tarball is avialble in the "Assets" section of each release. Be sure to
download the tarball that has a name of the form "ovis-ldms-X.X.X.tar.gz".

The links that are named "Source code (zip)" and "Source code (tar.gz)" are
automatic GitHub links that we are unable to remove. They will be missing the
configure script, because they are raw source from git repository and
not the official release tarball distribution.

Cloning the git repository
``````````````````````````

To clone the source code, go to https://github/com/ovis-hpc/ovis, and click
one the "Code" button. Or use the following command:

``git clone https://github.com/ovis-hpc/ovis.git -b OVIS-4``

Build Dependencies
------------------

* autoconf (>=2.63)
* automake
* libtool
* make
* bison
* flex
* libreadline
* openssl development library (for OVIS, LDMS Authentication)
* libmunge development library (for Munge LDMS Authentication plugin)
* Python >= 3.6 development library and Cython >= 0.29 (for the LDMS Python API and the LDMSD Interface, ldmsd_controller)
* doxygen (for the OVIS documentation)
* yyjson (optional, optimization for the avro_kafka storage plugin)

Some LDMS plug-ins have dependencies on additional libraries.

***REMARK*** Missing dependencies (e.g. python3-dev) may NOT break the
configuration and build but the features requiring them won't be built.

For cray-related LDMS sampler plug-in dependencies, please see the man page of the
plug-in in ``ldms/man/``.

RHEL7/CentOS7 dependencies
``````````````````````````

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

Compling the code
-----------------

If you are interested in storing LDMS data in SOS, then first
follow the instructions at https://github.com/ovis-hpc/sos to obtain,
build, and install SOS before proceding.

.. code:: sh

   cd <ovis source directory>
   sh autogen.sh
   ./configure [--prefix=<installation prefix>] [other options]
   make
   make install

Run ``configure --help`` for a full list of configure options.

Supported systems
=================

* Ubuntu and friends
* CentOS and friends
* Cray XE6, Cray XK, Cray XC

Unsupported features
====================

The following LDMS sampler plugins are considered unsupported. Use are your own risk:
* perfevent sampler
* hweventpapi sampler
* switchx

gnulib
------

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

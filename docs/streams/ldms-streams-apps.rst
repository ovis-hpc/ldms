Streams-enabled Application Data Collectors
#############################################

Caliper
***********************

This section covers the basic steps on how to compile, build and use the caliperConnector.

**What Is Caliper?**

A program instrumentation and performance measurement framework that allows users to implement analysiscapabilities (e.g. performance profiling, tracing, monitoring, and auto-tuning) into their applications using Caliper’s annotation API.

**What Is the caliperConnector?**

A Caliper-LDMS functionality that utilizes LDMS Streams to collect Caliper related data and absolute timestamp during runtime. It formats the data to a JSON message and *publishes* it to an LDMS streams interface.

Setup & Configuration
----------------------
Build the Caliper program with the application you wish to analyze. No modifications to the Caliper's instrumentations were required to integrate LDMS, so you will just need to follow the build and install instructions from `Caliper's Build and Install Webpage <https://software.llnl.gov/Caliper/CaliperBasics.html#build-and-install>`_

One built, you will need to poin the $LD_LIBRARY_PATH to Caliper's library:

.. code-block:: RST

  LD_LIBRARY_PATH=$LD_LIBRARY_PATH:<path-to-caliper-installation>/lib64

Now, to enable LDMS data collection, set (or export) the following list of caliper variables to ``ldms`` when executing a program. An example is shown below:

.. code-block:: RST

  CALI_LOOP_MONITOR_ITERATION_INTERVAL=10 ./caliper_example.o 400
  CALI_SERVICES_ENABLE=loop_monitor,mpi,ldms

The ``CALI_LOOP_MONITOR_ITERATION_INTERVAL`` collects measurements every n loop iterations of the acpplicaiton and the ``CALI_SERVICES_ENABLE`` define which services will be combined to collect the data.

Once done, you will just need to execute your program and you will have application data collected by Caliper and LDMS.

.. note::

  The MPI service (i.e., mpi) is required when enabling LDMS because it is used for associating the MPI rank data collected by LDMS.

LDMS Expected Output
--------------------
LDMS collects a set of runtime timeseries data of the application in parallel with Caliper. Below is an example output of the data collect, formatted into a JSON string:

.. code-block::

  {"job_id":11878171,"ProducerName":“n1","rank":0,"timestamp":1670373198.056455,"region":"init","time":33.172237 }
  {"job_id":11878171,"ProducerName":"n1","rank":0,"timestamp":1670373198.056455,"region":"initialization","time":33.211929 }
  {"job_id":11878171,"ProducerName":“n1","rank":0,"timestamp":1670373198.056455,"region":"main","time":44.147736 }
  {"job_id":11878171,"ProducerName":“n1","rank":0,"timestamp":1670373203.556555,"region":"main","time":0.049086 }
  {"job_id":11878171,"ProducerName":“n1","rank":0,"timestamp":1670373203.556555,"region":"run","time":0.049086 }

Any data collected by LDMS should have the same fields as the one shown above and can be viewed in a csv file **if** the LDMS csv_store plugin is configured in the LDMSD aggregator.

.. note::
  More information about starting and configuring and LDMS daemon to store to CSV can be found in `Run An LDMS Streams Daemon`_ or `LDMS Quickstart<ldms-quickstart>`_.



Darshan
***********************
This section covers basics steps on how to compile, build and use the Darshan-LDMS Integration code (i.e. darshanConnector). The following application tests are part of the Darshan program and can be found under ``<darshan-prefix>/darshan/darshan-test/regression/test-cases/src/``.

**What Is Darshan?**

A lightweight I/O characterization tool that transparently captures application I/O behavior from HPC applications with minimal overhead.

**What Is The darshanConnector?**

A Darshan-LDMS functionality that utilizes LDMS Streams to collect Darshan’s original I/O tracing, Darshan’s eXtended tracing (DXT) and absolute timestamp during runtime. It formats the data to a JSON message and *publishes* it to an LDMS streams interface. This data is a timeseries (i.e. absolute timestamp is collected) that will contain information about each individual I/O event.

.. image:: images/darshanConnector.png

        The above diagrams provieds a high level visualization of the darshanConnector. During the Darshan initialization, the connector (on the left-hand side) checks to see if darshan has been built against the ldms library and if it has it will initialize a connection to the LDMS stream daemon when the DARSHAN_LDMS_ENABLE is set. Once initialized, the connecter will know which module data we want to collect by checking which environment variables are set. For example, if MPI-IO_ENABLE_LDMS is set, that specific I/O event data will be collected. The runtime data collection and JSON message formatting is then performed in the darshan ldms connector send function. This function is triggered whenever an I/O event occurs. The data is then published to LDMS streams interface and sent to through the LDMS Transport to be stored into a database. As you can see at the very bottom left is the JSON formatted message. Meanwhile, on the right, darshan is running as usual by initializing their modules, collecting the I/O event data for these modules, aggregating and calculating the data and then outputting the information into a Darshan log file. As you can see, the LDMS Streams implementation does not interfere with Darshan

.. note::

  LDMS must already be installed on the system or locally. If it is not, then please following ``Getting The Source`` and ``Building The Source`` in the `LDMS Quickstart Guide <ldms-quickstart>`_. If the Darshan-LDMS code is already deployed on your system, please skip to `Run An LDMS Streams Daemon`_

**Metric Definitions**
Below are the list of Darshan metrics that are currently being collected by the darshanConnector:

* ``schema:`` Schema name of the data collected by the darshanConnector. This is an LDMS related metric and is only used for storing the data to the correct location in DSOS.

* ``module:`` Name of the Darshan module data being collected.

* ``uid:`` User ID of the job run.

* ``exe:`` Full path to the application executable. Only set to the full path when the "type" metric is set to "MET". Otherwise it is set to N/A.

* ``ProducerName:`` Name of the compute node the application is running on.

* ``switches:`` Number of times access alternated between read and write.

* ``file:`` Path to the filename of the I/O operations. Only set to the full path when the "type" metric is set to "MET". Otherwise it is set to N/A.

* ``rank:`` Rank of the processes at I/O

* ``flushes:`` Number of times the "flush" operation was performed. For H5F and H5D it is the HDF5 file flush and dataset flush operation counts, respectively.

* ``record_id:`` Darshan file record ID of the file the dataset belongs to.

* ``max_byte:`` Highest offset byte read and written (i.e. Darshan's "<MODULE\>\_MAX_BYTE_*" parameter).

* ``type:`` The type of json data being published. It is either set to MOD for gathering "module" data or MET for gathering static "meta" data (i.e. record id, rank ,etc.)

* ``job_id:`` The Job ID of the application run.

* ``op:`` Type of operation being performed (i.e. read, open, close, write).

* ``cnt:`` The count of the operations ("op" field) performed per module per rank. Resets to 0 after each "close" operation.

* ``seg:`` Contains the following array metrics from the operation ("op" field):

  ``pt_sel: HDF5 number of different access selections.
  reg_hslab: HDF5 number of regular hyperslabs.
  irreg_hslab: HDF5 number of irregular hyperslabs.
  ndims: HDF5 number of dimensions in dataset's dataspace.
  npoints: HDF5 number of points in dataset's dataspace.
  off: Cumulative total bytes read and cumulative total bytes written, respectively, for each module per rank. (i.e. Darshan's "offset" DXT parameter)
  len: Number of bytes read/written for the given operation per rank.
  start: Start time (seconds) of each I/O operation performed for the given rank
  dur: Duration of each operation performed for the given rank. (i.e. a rank takes "X" time to perform a r/w/o/c operation.)
  total: Cumulative time since the application run after the I/O operation (i.e. start of application + dur)
  timestamp: End time of given operation (i.e. "op" field) for the given rank (i.e. "rank" field). In epoch time.``

For all metric fields that don't apply to a module, a value of ``-1`` is given.

All data fields which that not change throughout the entire application run (i.e. constant), unless the darshanConnector is reconnected/restarted, are listed below:

* ``ProducerName``
* ``job_id``
* ``schema``
* ``exe``
* ``uid``


Compile and Build with LDMS
---------------------------
1. Run the following to build Darshan and link against an existing LDMS library on the system.

.. code-block:: RST

  git clone https://github.com/darshan-hpc/darshan.git
  cd darshan && mkdir build/
  ./prepare.sh && cd build/
  ../configure CC=<MPICC_WRAPPER> \
               --with-log-path-by-env=LOGFILE_PATH_DARSHAN \
               --prefix=<path-to-installation-directory>/darshan/<darshan_version> \
               --with-JOB_ID-env=<SCHED_JOB_ID> \
               --enable-ldms-mod \
               --with-ldms=<path_to_ldms_install>
  make && make install
.. note::

 * This configuration is specific to the system.  <MPICC_WRAPPER> should be replaced by the compiler wrapper for your MPI Library, (e.g., ``mpicc`` for Open MPI, or ``cc`` for Cray Development Environment MPI wrappers).
* If running an MPI program, make sure an MPI library is installed/loaded on the system.
  For more information on how to install and build the code across various platforms, please visit `Darshan's Runtime Installation Page   <https://www.mcs.anl.gov/research/projects/darshan/docs/darshan-runtime.html>`_
* ``--with-jobid-env=`` expects a string that is the environment variable that the hosted job scheduler utilizes on the HPC system.  (e.g., Slurm would use ``--with-jobid-env=SLURM_JOB_ID``)

2. **OPTIONAL** To build HDF5 module for Darshan, you must first load the HDF5 modulefile with ``module load hdf5-parallel``, then run configure as follows:

.. code-block:: RST

  ../configure CC=<MPICC_WRAPPER> \
               --with-log-path-by-env=LOGFILE_PATH_DARSHAN \
               --prefix=<path-to-installation-directory>/darshan/<darshan_version> \
               --with-jobid-env=<SCHED_JOB_ID> \
               --enable-ldms-mod \
               --with-ldms=<path_to_ldms_install>
               --enable-hdf5-mod \
               --with-hdf5=<path-to-hdf5-install>
  make && make install

2a. **OPTIONAL** If you do not have HDF5 installed on your system, you may install Python's ``h5py`` package with:

.. code-block:: RST

  sudo apt-get install -y hdf5-tools libhdf5-openmpi-dev openmpi-bin
  # we need to build h5py with the system HDF5 lib backend
  export HDF5_MPI="ON"
  CC=cc python -m pip install --no-binary=h5py h5py

.. note::

  If the HDF5 library is installed this way, you do not need to include the ``--with-hdf5`` flag during configuration. For more information on other methods and HDF5 versions to install, please visit `Darshan's Runtime Installation Page <https://www.mcs.anl.gov/research/projects/darshan/docs/darshan-runtime.html>`_.


Run an LDMS Streams Daemon
---------------------------
This section will go over how to start and configure a simple LDMS Streams deamon to collect the Darshan data and store to a CSV file.
If an LDMS Streams daemon is already running on the system then please skip to `Test the Darshan-LDMS Integrated Code (Multi Node)`_.

1. First, initialize an ldms streams daemon on a compute node as follows:

.. code-block:: RST

  salloc -N 1 --time=2:00:00 -p <partition-name>
  *ssh to node*

2. Once on the compute node (interactive session), set up the environment for starting an LDMS daemon:

.. code-block:: RST

  LDMS_INSTALL=<path_to_ldms_install>
  export LD_LIBRARY_PATH="$LDMS_INSTALL/lib/:$LDMS_INSTALL/lib:$LD_LIBRARY_PATH"
  export LDMSD_PLUGIN_LIBPATH="$LDMS_INSTALL/lib/ovis-ldms/"
  export ZAP_LIBPATH="$LDMS_INSTALL/lib/ovis-ldms"
  export PATH="$LDMS_INSTALL/sbin:$LDMS_INSTALL/bin:$PATH"
  export PYTHONPATH=<python-packages-path>
  export COMPONENT_ID="1"
  export SAMPLE_INTERVAL="1000000"
  export SAMPLE_OFFSET="0"
  export HOSTNAME="localhost"

.. note::

  LDMS must already be installed on the system or locally. If it is not, then please follow ``Getting The Source`` and ``Building The Source`` in the `LDMS Quickstart Guide <ldms-quickstart>`_.

3. Next, create a file called **"darshan\_stream\_store.conf"** and add the following content to it:

.. code-block:: RST

  load name=hello_sampler
  config name=hello_sampler producer=${HOSTNAME} instance=${HOSTNAME}/hello_sampler stream=darshanConnector component_id=${COMPONENT_ID}
  start name=hello_sampler interval=${SAMPLE_INTERVAL} offset=${SAMPLE_OFFSET}

  load name=stream_csv_store
  config name=stream_csv_store path=./streams/store container=csv stream=darshanConnector rolltype=3 rollover=500000

4.   Next, run the LDSM Streams daemon with the following command:

.. code-block:: RST

  ldmsd -x sock:10444 -c darshan_stream_store.conf -l /tmp/darshan_stream_store.log -v DEBUG -r ldmsd.pid

.. note::

  To check that the ldmsd daemon is connected running, run ``ps auwx | grep ldmsd | grep -v grep``, ``ldms_ls -h <hostname> -x sock -p <port> -a none -v`` or ``cat /tmp/darshan_stream_store.log``. Where <hostname> is the node where the LDMS daemon exists and <port> is the port number it is listening on.

Test the Darshan-LDMS Integrated Code (Multi Node)
---------------------------
This section gives step by step instructions on how to test the Darshan-LDMS Integrated code (i.e. darshanConnector) by executing a simple test application provided by Darshan.

Set The Environment
////////////////////
1. Once the LDMS streams daemon is initialized, **open another terminal window (login node)** and set the following environment variables before running an application test with Darshan:

.. code-block:: RST

  export DARSHAN_INSTALL_PATH=<path_to_darshan_install>
  export LD_PRELOAD=$DARSHAN_INSTALL_PATH/lib/libdarshan.so
  export LD_LIBRARY_PATH=$DARSHAN_INSTALL_PATH/lib:$LD_LIBRARY_PATH
  # optional. Please visit Darshan's webpage for more information.
  export DARSHAN_MOD_ENABLE="DXT_POSIX,DXT_MPIIO"

  # uncomment if hdf5 is enabled
  #export C_INCLUDE_PATH=$C_INCLUDE_PATH:/usr/include/hdf5/openmpi
  #export HDF5_LIB=<path_to_hdf5_install>/lib/libhdf5.so

  #set env variables for ldms streams daemon testing
  export DARSHAN_LDMS_STREAM=darshanConnector
  export DARSHAN_LDMS_XPRT=sock
  export DARSHAN_LDMS_HOST=<hostname>
  export DARSHAN_LDMS_PORT=10444
  export DARSHAN_LDMS_AUTH=none

  # enable LDMS data collection. No runtime data collection will occur if this is not exported.
  export DARSHAN_LDMS_ENABLE=

  # determine which modules we want to publish to ldmsd
  #export DARSHAN_LDMS_ENABLE_MPIIO=
  #export DARSHAN_LDMS_ENABLE_POSIX=
  #export DARSHAN_LDMS_ENABLE_STDIO=
  #export DARSHAN_LDMS_ENABLE_HDF5=
  #export DARSHAN_LDMS_ENABLE_ALL=
  #export DARSHAN_LDMS_VERBOSE=

.. note::

  The ``<hostname>`` is set to the node name the LDMS Streams daemon is running on (e.g. the node we previous ssh'd into). Make sure the ``LD_PRELOAD`` and at least one of the ``DARSHAN_LDMS_ENABLE_*`` variables are set. If not, no data will be collected by LDMS.

.. note::

  ``DARSHAN_LDMS_VERBOSE`` outputs the JSON formatted messages sent to the LDMS streams daemon. The output will be sent to STDERR.

Execute Test Application
/////////////////////////
Now we will test the darshanConnector with Darshan's example ``mpi-io-test.c`` code by setting the following environment variables:

.. code-block:: RST

  export PROG=mpi-io-test
  export DARSHAN_TMP=/tmp/darshan-ldms-test
  export DARSHAN_TESTDIR=<path_to_darshan_install>/darshan/darshan-test/regression
  export DARSHAN_LOGFILE_PATH=$DARSHAN_TMP

Now ``cd`` to the executable and test the appilcation with the darshanConnector enabled.

.. code-block:: RST

  cd darshan/darshan-test/regression/test-cases/src
  <MPICC_WRAPPER> $DARSHAN_TESTDIR/test-cases/src/${PROG}.c -o $DARSHAN_TMP/${PROG}
  cd $DARSHAN_TMP
  srun ${PROG} -f $DARSHAN_TMP/${PROG}.tmp.dat

Once the application is complete, to view the data please skip to `Check Results`_.

Test the Darshan-LDMS Integrated Code (Single Node)
----------------------------------
The section goes over step-by-step instructions on how to compile and execute the ``mpi-io-test.c`` program under ``darshan/darshan-test/regression/test-cases/src/``, collect the data with the LDMS streams daemon and store it to a CSV file on a single login node. This section is for those who will not be running their applications on a cluster (i.e. no compute nodes).

1. Set Environment Variables for Darshan, LDMS and Darshan-LDMS Integrated code (i.e. darshanConnector).

.. code-block:: RST

  # Darshan
  export DARSHAN_INSTALL_PATH=<path_to_darshan_install>
  export LD_PRELOAD=<path_to_darshan_install>/lib/libdarshan.so
  export LD_LIBRARY_PATH=$DARSHAN_INSTALL_PATH/lib:$LD_LIBRARY_PATH
  # Optional. Please visit Darshan's runtime webpage for more information.
  #export DARSHAN_MOD_ENABLE="DXT_POSIX,DXT_MPIIO"

  # uncomment if hdf5 is enabled
  #export C_INCLUDE_PATH=$C_INCLUDE_PATH:/usr/include/hdf5/openmpi
  #export HDF5_LIB=<path-to-hdf5-shared-libary-file>/libhdf5.so

  # LDMS

  LDMS_INSTALL=<path_to_ldms_install>
  export LD_LIBRARY_PATH="$LDMS_INSTALL/lib/:$LDMS_INSTALL/lib:$LD_LIBRARY_PATH"
  export LDMSD_PLUGIN_LIBPATH="$LDMS_INSTALL/lib/ovis-ldms/"
  export ZAP_LIBPATH="$LDMS_INSTALL/lib/ovis-ldms"
  export PATH="$LDMS_INSTALL/sbin:$LDMS_INSTALL/bin:$PATH"
  export PYTHONPATH=<python-packages-path>
  export COMPONENT_ID="1"
  export SAMPLE_INTERVAL="1000000"
  export SAMPLE_OFFSET="0"
  export HOSTNAME="localhost"

  # darshanConnector
  export DARSHAN_LDMS_STREAM=darshanConnector
  export DARSHAN_LDMS_XPRT=sock
  export DARSHAN_LDMS_HOST=<host-name>
  export DARSHAN_LDMS_PORT=10444
  export DARSHAN_LDMS_AUTH=none

  # enable LDMS data collection. No runtime data collection will occur if this is not exported.
  export DARSHAN_LDMS_ENABLE=

  # determine which modules we want to publish to ldmsd
  #export DARSHAN_LDMS_ENABLE_MPIIO=
  #export DARSHAN_LDMS_ENABLE_POSIX=
  #export DARSHAN_LDMS_ENABLE_STDIO=
  #export DARSHAN_LDMS_ENABLE_HDF5=
  #export DARSHAN_LDMS_ENABLE_ALL=
  #export DARSHAN_LDMS_VERBOSE=

.. note::

  ``DARSHAN_LDMS_VERBOSE`` outputs the JSON formatted messages sent to the LDMS streams daemon. The output will be sent to STDERR.

2. Generate the LDMSD Configuration File and Start the Daemon

.. code-block:: RST

  cat > darshan_stream_store.conf << EOF
  load name=hello_sampler
  config name=hello_sampler producer=${HOSTNAME} instance=${HOSTNAME}/hello_sampler stream=darshanConnector component_id=${COMPONENT_ID}
  start name=hello_sampler interval=${SAMPLE_INTERVAL} offset=${SAMPLE_OFFSET}

  load name=stream_csv_store
  config name=stream_csv_store path=./streams/store container=csv stream=darshanConnector rolltype=3 rollover=500000
  EOF

  ldmsd -x sock:10444 -c darshan_stream_store.conf -l /tmp/darshan_stream_store.log -v DEBUG
  # check daemon is running
  ldms_ls -p 10444 -h localhost -v

3. Set Up Test Case Variables

.. code-block:: RST

  export PROG=mpi-io-test
  export DARSHAN_TMP=/tmp/darshan-ldms-test
  export DARSHAN_TESTDIR=<path_to_darshan_install>/darshan/darshan-test/regression
  export DARSHAN_LOGFILE_PATH=$DARSHAN_TMP

4. Run Darshan's mpi-io-test.c program

.. code-block:: RST

  cd darshan/darshan-test/regression/test-cases/src
  <MPICC_WRAPPER> $DARSHAN_TESTDIR/test-cases/src/${PROG}.c -o $DARSHAN_TMP/${PROG}
  cd $DARSHAN_TMP
  ./${PROG} -f $DARSHAN_TMP/${PROG}.tmp.dat

Once the application is complete, to view the data please skip to `Check Results`_.

Pre-Installed Darshan-LDMS
---------------------------
If both the Darshan-LDMS integrated code (i.e., darshanConnector) and LDMS are already installed, and a system LDMS streams daemon is running, then there are two ways to enable the LDMS functionality:

1. Set the environment via sourcing the ``darshan_ldms.env`` script 

2. Load the Darshan-LDMS module via ``module load darshan_ldms`` 

.. note::

  Only when executing an application or submitting a job does the user need to load the ``darshan_ldms`` modulefile or source the ``darshan_ldms.env`` script.  Compiling, building, or installing the application does not affect the darshanConnector and vice versa. 

1. Set Environment
///////////////////

In order to enable the darshanConnector code on the system, just source the following env script:

.. code-block:: RST

  module use /projects/ovis/modules/<system>
  source /projects/ovis/modules/<system>/darshan_ldms.env

**OPTIONAL**: Add a "-v" when sourcing this file to enable verbose:

.. code-block:: RST

  $ source /projects/ovis/modules/<system>/darshan_ldms.env -v

This will output json messages collected by ldms to the terminal window.

.. note::

  The STDIO data will NOT be collected by LDMS. This is to prevent any recursive LDMS function calls. 

2. Load Module
///////////////

If you do not wish to set the environment using the env script from above, you can always load the ``darshan_ldms`` modulefile, as follows:

.. code-block:: RST

  module use /projects/ovis/modules/<system>
  module load darshan_ldms

**OPTIONAL**: If you decide to load the module, you will need to turn on verbose by setting the following environment variable in your run script:

.. code-block:: RST
  export DARSHAN_LDMS_VERBOSE="true"

Script Information
///////////////////

The darshan_ldms module and .env file set the following env variables to define where the Darshan install is located, the LDMS daemon connection and what kind of file level access data will be published and stored to DSOS (via LDMS streams).

If you only want to collect a specific type of data such as "MPIIO" then you will only set the ``DARSHAN_LDMS_ENABLE_MPIIO`` variable:

.. code-block:: RST
  export DARSHAN_LDMS_ENABLE_MPIIO=""

If you want to collect all types of data then set all *_ENABLE_LDMS variables:

.. code-block:: RST
  export DARSHAN_LDMS_ENABLE_MPIIO=""
  export DARSHAN_LDMS_ENABLE_POSIX=""
  export DARSHAN_LDMS_ENABLE_HDF5=""

.. note::

  All Darshan binary log-files (i.e. <executable-name>.darshan) will be saved to ``$LOGFILE_PATH_DARSHAN``, as specified at build time and exported in the user environment.

.. code-block:: RST

  # Set variables for darshan install
  export LD_PRELOAD=$LD_PRELOAD:$DARSHAN_INSTALL_PATH/lib/libdarshan.so
  export PATH=$PATH:$DARSHAN_INSTALL_PATH/bin
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$DARSHAN_INSTALL_PATH/lib
  export LIBRARY_PATH=$LIBRARY_PATH:$DARSHAN_INSTALL_PATH/lib

  export DARSHAN_RUNTIME_DIR=$DARSHAN_INSTALL_PATH
  export DARSHAN_RUNTIME_BIN=$DARSHAN_INSTALL_PATH/bin
  export DARSHAN_RUNTIME_LIB=$DARSHAN_INSTALL_PATH/lib
  export HDF5_USE_FILE_LOCKING=1

  # Set logfile path
  export DARSHAN_TMP=/projects/ovis/darshanConnector/<system>/darshan/build/logs/
  export LOGFILE_PATH_DARSHAN=$DARSHAN_TMP

  # Connect to ldms daemon
  export DARSHAN_LDMS_STREAM=darshanConnector
  export DARSHAN_LDMS_PORT=412
  export DARSHAN_LDMS_HOST=localhost
  export DARSHAN_LDMS_XPRT=sock
  export DARSHAN_LDMS_AUTH=munge

  # Specify type of data to collect
  export DARSHAN_LDMS_ENABLE=
  export DARSHAN_LDMS_ENABLE_MPIIO=
  export DARSHAN_LDMS_ENABLE_POSIX=
  export DARSHAN_LDMS_ENABLE_STDIO=
  export DARSHAN_LDMS_ENABLE_HDF5=
  #export DARSHAN_LDMS_ENABLE_ALL=
  #export DARSHAN_LDMS_VERBOSE=

  # check if verbose is requested
  if [ "$1" == "-v" ]; then
          export DARSHAN_LDMS_VERBOSE=
          echo "Verbose is set."
  else
          unset DARSHAN_LDMS_VERBOSE
  fi


Run application
///////////////
Once the module is loaded and the environment is set, you will just need to run your application. All darshan related logs will automatically be saved in the directory specified in ``$LOGFILE_PATH_DARSHAN``.

.. note::

  If runtime errors or issues occur, then this is most likely due to incompatibility issues with the application build, or the Darshan-LDMS build that is using ``LD_PRELOAD``. You may debug the issue, as follows:

  1. Unset the ``LD_PRELOAD`` environment variable (e.g., ``unset LD_PRELOAD``), then run the application with: ``mpiexec -env LD_PRELOAD $DARSHAN_INSTALL_PATH/lib/libdarshan.so`` or ``srun --export=LD_PRELOAD=$DARSHAN_INSTALL_PATH/lib/libdarshan.so``.
  For more information please see section 5.2 in `Darshan's Runtime Installation Page <https://www.mcs.anl.gov/research/projects/darshan/docs/darshan-runtime.html>`_.

  2. If you are still running into runtime issues, please send an email to ldms@sandia.gov and provide:
    a) mpi-io, hdf5, pnetcdf, compiler version (if applicable) used to build your application
    b) Contents of your environment variables: $PATH, $LIBRARY_PATH, $LD_LIBRARY_PATH and $LD_PRELOAD.


Check Results
-------------
LDMS Output
////////////
This section provides the expected output of an application run with the data published to LDMS streams daemon with a CSV storage plugin (see section `Run An LDMS Streams Daemon`_).

* If you are publishing to a Local Streams Daemon (compute or login nodes) to collect the Darshan data, then compare the generated ``csv`` file to the one shown below in this section.

* If you are publishing to a System Daemon, that aggregates the data and stores to a Scalable Object Store (SOS), please skip this section and go to the :doc:`SOS Quickstart Guide <sos-quickstart>` for more information about viewing and accessing data from this database.

LDMS Log File
/////////////
*   Once the application has completed, run ``cat /tmp/hello_stream_store.log`` in the terminal window where the ldmsd is running (compute node). You should see a similar output to the one below.

.. code-block:: RST

  cat /tmp/hello_stream_store.log
  Fri Feb 18 11:35:23 2022: INFO  : stream_type: JSON, msg: "{ "job_id":53023,"rank":3,"ProducerName":"nid00052","file":"darshan-output/mpi-io-test.tmp.dat","record_id":1601543006480890062,"module":"POSIX","type":"MET","max_byte":-1,"switches":-1,"flushes":-1,"cnt":1,"op":"opens_segment","seg":[{"data_set":"N/A","pt_sel":-1,"irreg_hslab":-1,"reg_hslab":-1,"ndims":-1,"npoints":-1,"off":-1,"len":-1,"dur":0.00,"timestamp":1645209323.082951}]}", msg_len: 401, entity: 0x155544084aa0
  Fri Feb 18 11:35:23 2022: INFO  : stream_type: JSON, msg: "{ "job_id":53023,"rank":3,"ProducerName":"nid00052","file":"N/A","record_id":1601543006480890062,"module":"POSIX","type":"MOD","max_byte":-1,"switches":-1,"flushes":-1,"cnt":1,"op":"closes_segment","seg":[{"data_set":"N/A","pt_sel":-1,"irreg_hslab":-1,"reg_hslab":-1,"ndims":-1,"npoints":-1,"off":-1,"len":-1,"dur":0.00,"timestamp":1645209323.083581}]}", msg_len: 353, entity: 0x155544083f60
  ...

CSV File
////////
* To view the data stored in the generated CSV file from the streams store plugin, kill the ldmsd daemon first by running: ``killall ldmsd``
* Then ``cat`` the file in which the CSV file is located. Below is the stored DXT module data from LDMS's streams\_csv_\_store plugin for the ``mpi-io-test-dxt.sh`` test case.

.. code-block:: RST

  #module,uid,ProducerName,switches,file,rank,flushes,record_id,exe,max_byte,type,job_id,op,cnt,seg:off,seg:pt_sel,seg:dur,seg:len,seg:ndims,seg:reg_hslab,seg:irreg_hslab,seg:data_set,seg:npoints,seg:timestamp,seg:total,seg:start
  POSIX,99066,n9,-1,/lustre/<USER>/darshan-ldms-output/mpi-io-test_lC.tmp.out,278,-1,9.22337E+18,/lustre/<USER>/darshan-ldms-output/mpi-io-test,-1,MET,10697754,open,1,-1,-1,0.007415,-1,-1,-1,-1,N/A,-1,1662576527,0.007415,0.298313
  MPIIO,99066,n9,-1,/lustre/<USER>/darshan-ldms-output/mpi-io-test_lC.tmp.out,278,-1,9.22337E+18,/lustre/<USER>/darshan-ldms-output/mpi-io-test,-1,MET,10697754,open,1,-1,-1,0.100397,-1,-1,-1,-1,N/A,-1,1662576527,0.100397,0.209427
  POSIX,99066,n11,-1,/lustre/<USER>/darshan-ldms-output/mpi-io-test_lC.tmp.out,339,-1,9.22337E+18,/lustre/<USER>/darshan-ldms-output/mpi-io-test,-1,MET,10697754,open,1,-1,-1,0.00742,-1,-1,-1,-1,N/A,-1,1662576527,0.00742,0.297529
  POSIX,99066,n6,-1,/lustre/<USER>/darshan-ldms-output/mpi-io-test_lC.tmp.out,184,-1,9.22337E+18,/lustre/<USER>/darshan-ldms-output/mpi-io-test,-1,MET,10697754,open,1,-1,-1,0.007375,-1,-1,-1,-1,N/A,-1,1662576527,0.007375,0.295111
  POSIX,99066,n14,-1,/lustre/<USER>/darshan-ldms-output/mpi-io-test_lC.tmp.out,437,-1,9.22337E+18,/lustre/<USER>/darshan-ldms-output/mpi-io-test,-1,MET,10697754,open,1,-1,-1,0.007418,-1,-1,-1,-1,N/A,-1,1662576527,0.007418,0.296812
  POSIX,99066,n7,-1,/lustre/<USER>/darshan-ldms-output/mpi-io-test_lC.tmp.out,192,-1,9.22337E+18,/lustre/<USER>/darshan-ldms-output/mpi-io-test,-1,MET,10697754,open,1,-1,-1,0.007435,-1,-1,-1,-1,N/A,-1,1662576527,0.007435,0.294776
  MPIIO,99066,n7,-1,/lustre/<USER>/darshan-ldms-output/mpi-io-test_lC.tmp.out,192,-1,9.22337E+18,/lustre/<USER>/darshan-ldms-output/mpi-io-test,-1,MET,10697754,open,1,-1,-1,0.033042,-1,-1,-1,-1,N/A,-1,1662576527,0.033042,0.273251
  ...

Compare With Darshan Log File(s)
////////////////////////////////
Parse the Darshan binary file using Darshan's standard and DXT (only if the ``DXT Module`` is enabled) parsers.

.. code-block:: RST

  $DARSHAN_INSTALL_PATH/bin/darshan-parser --all $LOGFILE_PATH_DARSHAN/<name-of-logfile>.darshan > $DARSHAN_TMP/${PROG}.darshan.txt
  $DARSHAN_INSTALL_PATH/bin/darshan-dxt-parser --show-incomplete $LOGFILE_PATH_DARSHAN/<name-of-logfile>.darshan > $DARSHAN_TMP/${PROG}-dxt.darshan.txt

Now you can view the log(s) with ``cat $DARSHAN_TMP/${PROG}.darshan.txt`` or ``cat $DARSHAN_TMP/${PROG}-dxt.darshan.txt`` and compare them to the data collected by LDMS.

The ``producerName``, file path and record_id of each job should match and, if ``dxt`` was enabled, the individual I/O statistics of each rank (i.e., start time and number of I/O operations).


Kokkos
***********************
* Appropriate Kokkos function calls must be included in the application code. Add the following environmental variables to your run script to push Kokkos data from the application to stream for collection.

**What Is Kokkos?**

A C++ parallel programming ecosystem for performance portability across multi-core, many-core, and GPU node architectures. Provides abstractions of parallel execution of code and data management.

Setup and Configuration
----------------------
**The KokkosConnector**

A Kokkos-LDMS functionality that utilizes LDMS Streams to collect Kokkos related data during runtime. Kokkos sampler, provided by the Kokkos-tools library, controls the sampling rate and provides the option to sample data using a count-based push. It then formats the data to a JSON message and *publishes* it to an LDMS streams interface.

.. warning::
    To use kokkosConnector, all users will need to install Kokkos-Tools. You can find their repository and instructions on installing it here: https://github.com/kokkos/kokkos-tools


The following environmental variables are needed in an application's runscript to run the kokkos-sampler and LDMS's kokkosConnector:

.. code-block:: RST

  export KOKKOS_LDMS_HOST="localhost"
  export KOKKOS_LDMS_PORT="412"
  export KOKKOS_PROFILE_LIBRARY="<insert install directory>/kokkos-tools/common/kokkos_sampler/kp_sampler.so;<insert install directory>/ovis/kokkosConnector/kp_kernel_ldms.so"
  export KOKKOS_SAMPLER_RATE=101
  export KOKKOS_LDMS_VERBOSE=0
  export KOKKOS_LDMS_AUTH="munge"
  export KOKKOS_LDMS_XPRT="sock"

* The KOKKOS_SAMPLER_RATE variable determines the rate of messages pushed to streams and collected. Please note that it is in best practice to set this to a prime number to avoid collecting information from the same kernels.
* The KOKKOS_LDMS_VERBOSE variable can be set to 1 for debug purposes which prints all collected kernel data to the console.

How To Make A Data Connector
*****************************
In order to create a data connector with LDMS to collect runtime timeseries application data, you will need to utilize LDMS's Streams Functionality. This section will provide the necessary functions and Streams API required to make the data connector.

The example (code) below is pulled from the Darshan-LDMS Integration code.

.. note::

  The LDMS Streams functionality uses a push-based method to reduce memory consumed and data loss on the node.

Include the following LDMS files
---------------------------------------
* First, the following libaries will need to be included in the program as these contain all the functions that the data connector will be using/calling.
.. code-block:: RST

  #include <ldms/ldms.h>
  #include <ldms/ldmsd_stream.h>
  #include <ovis_util/util.h>

Initialize All Necessary Variables
-----------------------------------

* Next, the following variables will need to be initialized globally or accessible by the Streams API Functions described in the next section:

.. code-block:: RST

  #define SLURM_NOTIFY_TIMEOUT 5
  ldms_t ldms_g;
  pthread_mutex_t ln_lock;
  int conn_status, to;
  ldms_t ldms_darsh;
  sem_t conn_sem;
  sem_t recv_sem;


Copy "Hello Sampler" Streams API Functions
------------------------------------------
Next, copy the ``ldms_t setup_connection`` and ``static void event_cb`` functions listed below. These functions originated from the `ldmsd_stream_subscribe.c <https://github.com/ovis-hpc/ovis/blob/OVIS-4/ldms/src/ldmsd/test/ldmsd_stream_subscribe.c>`_ code.

The ``setup_connection`` contains LDMS API calls that connects to the LDMS daemon and the  ``static void event_cb`` is a callback function to check the connection status of the LDMS Daemon.

.. code-block:: RST

  static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
  {
          switch (e->type) {
          case LDMS_XPRT_EVENT_CONNECTED:
                  sem_post(&conn_sem);
                  conn_status = 0;
                  break;
          case LDMS_XPRT_EVENT_REJECTED:
                  ldms_xprt_put(x);
                  conn_status = ECONNREFUSED;
                  break;
          case LDMS_XPRT_EVENT_DISCONNECTED:
                  ldms_xprt_put(x);
                  conn_status = ENOTCONN;
                  break;
          case LDMS_XPRT_EVENT_ERROR:
                  conn_status = ECONNREFUSED;
                  break;
          case LDMS_XPRT_EVENT_RECV:
                  sem_post(&recv_sem);
                  break;
          case LDMS_XPRT_EVENT_SEND_COMPLETE:
                  break;
          default:
                  printf("Received invalid event type %d\n", e->type);
          }
  }

  ldms_t setup_connection(const char *xprt, const char *host,
                          const char *port, const char *auth)
  {
          char hostname[PATH_MAX];
          const char *timeout = "5";
          int rc;
          struct timespec ts;

          if (!host) {
                  if (0 == gethostname(hostname, sizeof(hostname)))
                          host = hostname;
          }
          if (!timeout) {
                  ts.tv_sec = time(NULL) + 5;
                  ts.tv_nsec = 0;
          } else {
                  int to = atoi(timeout);
                  if (to <= 0)
                          to = 5;
                  ts.tv_sec = time(NULL) + to;
                  ts.tv_nsec = 0;
          }

          ldms_g = ldms_xprt_new_with_auth(xprt, auth, NULL);
          if (!ldms_g) {
                  printf("Error %d creating the '%s' transport\n",
                         errno, xprt);
                  return NULL;
          }

          sem_init(&recv_sem, 1, 0);
          sem_init(&conn_sem, 1, 0);

          rc = ldms_xprt_connect_by_name(ldms_g, host, port, event_cb, NULL);
          if (rc) {
                  printf("Error %d connecting to %s:%s\n",
                         rc, host, port);
                  return NULL;
          }
          sem_timedwait(&conn_sem, &ts);
          if (conn_status)
                  return NULL;
          return ldms_g;
  }

Initialize and Connect to LDMSD
------------------------------------------
Once the above functions have been copied, the ``setup_connection`` will need to be called in order to establish a connection an LDMS Streams Daemon.

.. note::

  The LDMS Daemon is configured with the  `Streams Plugin <https://github.com/ovis-hpc/ovis/blob/OVIS-4/ldms/src/sampler/hello_stream/Plugin_hello_sampler.man>`_ and should already be running on the node. The host is set to the node the daemon is running on and port is set to the port the daemon is listening to. Below you will find an example of the Darshan Connector for reference.

.. code-block:: RST

  Updates Comming Soon

The environment variables ``DARSHAN_LDMS_X`` are used to define the stream name (configured in the daemon), transport type (sock, ugni, etc.), host, port and authentication of the LDMSD. In this specific example, the stream name is set to "darshanConnector" so the environment variable, ``DARSHAN_LDMS_STREAM`` is exported as follows: ``export DARSHAN_LDMS_STREAM=darshanConnector``

.. note::
   The environment variables are not required. The stream, transport, host, port and authentication can be initialized and set within in the code.

.. note::
    If you run into the following error: ``error:unknown type name 'sem_t'`` then you will need to add the following libraries to your code:

    * ``#include <ldms/ldms_xprt.h>``
    * ``#include <semaphore.h>``

Publish Event Data to LDMSD
-------------------------------------
Now we will create a function that will collect all relevent application events and publish to the LDMS Streams Daemon. In the Darshan-LDMS Integration, the following Darshan's I/O traces for each I/O event (i.e. open, close, read, write) are collected along with the absolute timestamp (for timeseries data) for each I/O event:

.. code-block:: RST

  Updates Coming Soon

.. note::

  For more information about the various Darshan I/O traces and metrics collected, please visit `Darshan's Runtime Installation Page <https://www.mcs.anl.gov/research/projects/darshan/docs/darshan-runtime.html>`_ and `Darshan LDMS Metrics Collected <https://github.com/Snell1224/darshan/wiki/Darshan-LDMS---Metric-Definitions>`_ pages.

Once this function is called, it initializes a connection to the LDMS Streams Daemon, attempts reconnection if the connection is not established, then formats the given arguements/variables into a JSON message format and finally publishes to the LDMS Streams Deamon.

There are various types of formats that can be used to publish the data (i.e. JSON, string, etc.) so please review the `Defining A Format`_ section for more information.

Collect Event Data
/////////////////////////

To collect the application data in real time (and using the example given in this section), the ``void darshan_ldms_connector_send(arg1, arg2, arg3,....)`` will be placed in all sections of the code where we want to publish a message. From the Darshan-LDMS Integration code we would have:

.. code-block:: RST

  darshan_ldms_connector_send(rec_ref->file_rec->counters[MPIIO_COLL_OPENS] + rec_ref->file_rec->counters[MPIIO_INDEP_OPENS], "open", -1, -1, -1, -1, -1, __tm1, __tm2, __ts1, __ts2, rec_ref->file_rec->fcounters[MPIIO_F_META_TIME], "MPIIO", "MET");

This line of code is placed within multiple macros (`MPIIO_RECORD_OPEN/READ/WRITE <https://github.com/darshan-hpc/darshan/blob/main/darshan-runtime/lib/darshan-mpiio.c>`_) in Darshan's MPIIO module.

* Doing this will call the function everytime Darshan detects an I/O event from the application (i.e. read, write, open, close). Once called, the arguements will be passed to the function, added to the JSON formatted message and pushed to the LDMS daemon.

.. note::

  For more information about how to store the published data from and LDMS Streams Daemon, please see the Stream CSV Store plugin man pages on a system where LDMS Docs are installed: ``man  Plugin_stream_csv_store``

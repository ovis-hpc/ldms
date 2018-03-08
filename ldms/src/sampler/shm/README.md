# Application Performance Data Streaming for System Monitoring


## Table of Contents
* [Introduction](#introduction)
* [Overview](#overview)
* [MPI Sampler](#mpi-sampler)
	* [Dependencies](#dependencies)
	* [Building OVIS with MPI Sampler](#building-ovis-with-mpi-sampler)
	* [Usage](#usage)
		* [Running the LDMS Sampler](#running-the-ldms-sampler)
		* [Running the MPI application as the data provider](#running-the-mpi-application-as-the-data-provider)
			* [MPI Profiler Configuration Options](#mpi-profiler-configuration-options)
	    * [Example](#example)


# Introduction 

__shm_sampler__ is a sampler plug-in module within the the LDMS. This sampler can read from a dynamic
number of shm files. These files are tracked by a central index file in shared memory. 
The main usage of this sampler is to stream  application performance data.
[top](#table-of-contents)



# Overview  

To stream  application performance data, three components are involved: an application profiler, a shared memory index, and a sampler. 
The application profiler collects information about the software level events. 
The shared memory index  provides a mechanism to access the data collected by the application profiler. 
The sampler  utilizes the shared memory index to periodically expose the data collected by the application profiler.
[top](#table-of-contents)


# MPI Sampler 
Different application profilers can be used as the data source for shm_sampler. Currently, an MPI profiler is included and can be used to stream MPI application software level events.
[top](#table-of-contents)


## Dependencies
In addition to common LDMS dependencies, an MPI library should be available on the target system.
[top](#table-of-contents)


## Building OVIS with MPI Sampler
To enable OVIS with MPI sampler,  the following flag must be given at the configure line:
```
	--enable-mpi_sampler
```
If a non-default MPI compiler is used its path should be specified using MPICC variable. e.g.
```
	MPICC=<PATH TO MPI LIBRARY> ../configure --prefix=<installed path> [options]
```

> **Note:** Same MPI library should be used for building the target application,  OVIS, and running the application.
[top](#table-of-contents)


## Usage
To stream MPI application performance data, LDMS shm_sampler should be run as the data consumer and the MPI application linked with the MPI profiler should be run as the data provider.
[top](#table-of-contents)


### Running the LDMS Sampler
The following configurations are available to run the shm_sampler and if the MPI profiler is used as the data provider, it streams MPI application performance data.
```
    config name=shm_sampler producer=<name> instance=<name> [shm_index=<name>][shm_boxmax=<int>][shm_array_max=<int>][shm_metric_max=<int>] [shm_set_timeout=<int>][component_id=<int>] [schema=<name>] [job_set=<name> job_id=<name> app_id=<name> job_start=<name> job_end=<name>]
	producer     A unique name for the host providing the data
	instance     A unique name for the metric set
	shm_index    A unique name for the shared memory index file
	shm_boxmax   Maximum number of entries in the shared memory index file
	shm_array_max   Maximum number of elements in array metrics
	shm_metric_max  Maximum number of metrics
	shm_set_timeout No read/write timeout in seconds
	component_id A unique number for the component being monitored, Defaults to zero.
	schema       The name of the metric set schema, Defaults to the sampler name
	job_set      The instance name of the set containing the job data, default is 'job_info'
	job_id       The name of the metric containing the Job Id, default is 'job_id'
	app_id       The name of the metric containing the Application Id, default is 'app_id'
	job_start    The name of the metric containing the Job start time, default is 'job_start'
	job_end      The name of the metric containing the Job end time, default is 'job_end'
```
Here is an example configuration:
```
load name=shm_sampler
config name=shm_sampler producer=samplerd instance=samplerd/shm_sampler shm_index=/ldms_shm_mpi_index
start name=shm_sampler interval=1000000 offset=0
```
> **Note:** The value for __shm_index__ should be the same as the shared memory index name that is used by the profiler.

[top](#table-of-contents)


### Running the MPI application as the data provider
To collect MPI application events, it should be linked to the MPI profiler. If dynamic linking is possible, there is no need to recompile or relink the application. The only command that should be run before the application is the ```LD_PRELOAD```command that makes the application to  link with the profiler to collect events.
Currently, the MPI profiler shared library named ```libldms_mpi_profiler.so```is in the following directory: ```$LDMS_INSTALL_PATH/lib/ovis-ldms```
This shared library should be loaded using ```LD_PRELOAD```command:
```
LDMS_MPI_PROFILER_LIB_NAME=libldms_mpi_profiler.so
LDMS_MPI_PROFILER_PATH=$LDMS_INSTALL_PATH/lib/ovis-ldms/$LDMS_MPI_PROFILER_LIB_NAME
export LD_PRELOAD=$LDMS_MPI_PROFILER_PATH:$LD_PRELOAD
```
> **Note:** In some cases you might see an error like this after running the MPI application where the MPI profiler library is preloaded:
> ```mpirun: symbol lookup error: $LDMS_INSTALL_PATH/lib/ovis-ldms/$LDMS_MPI_PROFILER_LIB_NAME: undefined symbol: ompi_mpi_comm_world```
> To resolve this issue, add the ```libmpi.so``` path to the ```LD_PRELOAD```. e.g.:
>``` export LD_PRELOAD=$LDMS_MPI_PROFILER_PATH:/usr/lib64/openmpi/lib/libmpi.so:$LD_PRELOAD```

[top](#table-of-contents)


#### MPI Profiler Configuration Options
Currently, MPI profiler uses environment variables to get the user options. The following variables are available:

| Variable Name  				  | Type    |  Example                  |  Notes
|---------------------------------|---------|---------------------------|--------
|LDMS_SHM_INDEX  				  | String  |```"/ldms_shm_mpi_index"```| A unique name for the shared memory index file. The value for this variable must be the same as the value for __shm_index__ in __shm_sampler__ configurations. If this variable is not provided the profiling will be disabled.
|LDMS_SHM_MPI_PROFILER_LOG_LEVEL  | Integer | 1 						| The log level for the MPI profiler. Value of ```0``` will disable the profiler. Value of ```1 ``` will enable the profiler with minimum log information. Value of ```2 ``` will print out more log information during the profiling.
|LDMS_SHM_MPI_FUNC_INCLUDE  	  | String  | ```"MPI_Send,MPI_Recv"``` | The configurations for events. More notes about this option are included below this table. 
|LDMS_SHM_MPI_STAT_SCOPE          | Integer | 1 						| Data collection granularity mode. Value of ```0``` will assign one global counter for each event. Value of ```1``` will assign one counter per MPI rank for each event. 
|LDMS_SHM_MPI_EVENT_UPDATE        | Integer | 1 						| Event update type. Value of ```0``` will create a local thread that updates event coutners in the shared memory index periodically. Value of ```1``` will update event counters in the shared memory index immeidately. 

To configure specifc events the string value for the ```LDMS_SHM_MPI_FUNC_INCLUDE``` variable will be parsed. The following delitmiters are available for determining events:

| Delimiter| What?   		  |  Example                  |  Notes
|----------|------------------|---------------------------|--------
|```','``` | Base event configuration  | ```"MPI_Send,MPI_Recv"``` | Each base event (MPI function) configuration, should be separated by ```','```.
|```':'``` | Base event and Event type  | ```"MPI_Send:calls,MPI_Recv:calls"``` | Base event (MPI function) should be separated from the event type using ```':'```.
|```'#'``` | Event types  | ```"MPI_Send:calls#bytes,MPI_Recv:calls#bytes"``` | Different event types should be separated using ```'#'```. ```calls``` will create an event to count number of calls. ```bytes``` will create an event to count the size of the messages.
|```'@'``` | Argument Filtering  | ```"MPI_Send:calls#bytes@5<size<14,MPI_Recv@size<10"``` | Filtering based on argument should be separeted from the rest of configuration using ```'@'```.
|```'<'``` | Message size Filtering  | ```"MPI_Send:calls#bytes@5<size<14,MPI_Recv@size<10"``` | Filtering based on message size will be determined using ```'<'```.
[top](#table-of-contents)

### Example
An example of a configuration for shm_sampler is available here:
``` ldms/src/sampler/shm/test/samplerd.conf ```
An example of a configuration for mpi_profiler is available here:
ldms/src/sampler/shm/test/example-mpi_profiler-conf.sh
> **Note:** Please note that the ```LDMS_INSTALL_PATH``` variable in this script should be updated to the actual LDMS install path on your system.

[top](#table-of-contents)

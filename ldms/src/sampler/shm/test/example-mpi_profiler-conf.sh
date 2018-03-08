#!/bin/bash
# Currently we rely on environment variables to configure the events.

### CHANGE INSTALL PATH HERE
LDMS_INSTALL_PATH=/home/ramin/src/ovis/mpi-sampler/build

### CHANGE MPI APP PATH HERE.
MPI_APP_PATH=$LDMS_INSTALL_PATH/bin/MPIApp

MPI_RUN_COMMAND=mpirun

LDMS_MPI_PROFILER_LIB_NAME=libldms_mpi_profiler.so
LDMS_MPI_PROFILER_PATH=$LDMS_INSTALL_PATH/lib/ovis-ldms/$LDMS_MPI_PROFILER_LIB_NAME


# Name of the shared index. This should be the same in the mpi sampler configuration (shm_index=...).
export LDMS_SHM_INDEX="/ldms_shm_mpi_index"

#Log level
export LDMS_SHM_MPI_PROFILER_LOG_LEVEL=1

# Example configuration 1
declare MSG_CONF1="Example configuration 1: Profile these functions: MPI_Send,MPI_Recv. Scope: collect global counters, i.e. only one metric for all calls by all mpi processes to MPI_Send and another one for all calls to MPI_Recv"
declare CONF1="LDMS_SHM_MPI_FUNC_INCLUDE=\"MPI_Send,MPI_Recv\" LDMS_SHM_MPI_STAT_SCOPE=0"


# Example configuration 2
declare MSG_CONF2="Example configuration 2: Profile these functions: MPI_Send,MPI_Recv. Scope: collect process local counters, i.e. one metric for all calls by each mpi processes to MPI_Send and another one for all calls to MPI_Recv by each process"
declare CONF2="LDMS_SHM_MPI_FUNC_INCLUDE=\"MPI_Send,MPI_Recv\" LDMS_SHM_MPI_STAT_SCOPE=1"


# Example configuration 3
declare MSG_CONF3="Example configuration 3: Count number of calls to MPI_Send and the amount of bytes were sent - count all number of calls to MPI_Recv"
declare CONF3="LDMS_SHM_MPI_FUNC_INCLUDE=\"MPI_Send:calls#bytes,MPI_Recv\" LDMS_SHM_MPI_STAT_SCOPE=1"


# Example configuration 4
declare MSG_CONF4="Example configuration 4: Count number of calls to MPI_Send and MPI_Recv and the amount of bytes were sent and received"
declare CONF4="LDMS_SHM_MPI_FUNC_INCLUDE=\"MPI_Send:calls#bytes,MPI_Recv:calls#bytes\" LDMS_SHM_MPI_STAT_SCOPE=1"


# Example configuration 5
declare MSG_CONF5="Example configuration 5: Count number of calls to MPI_Recv and the amount of bytes were sent and received"
declare CONF5="LDMS_SHM_MPI_FUNC_INCLUDE=\"MPI_Send:bytes,MPI_Recv:calls#bytes\" LDMS_SHM_MPI_STAT_SCOPE=1"


# Example configuration 6
declare MSG_CONF6="Example configuration 6: Count number of calls to MPI_Send and amount of bytes were sent for calls that  size of the message was between 5 and 14 bytes. Count number of calls to MPI_Recv that size of the message was less than 10 bytes"
declare CONF6="LDMS_SHM_MPI_FUNC_INCLUDE=\"MPI_Send:calls#bytes@5<size<14,MPI_Recv@size<10\" LDMS_SHM_MPI_STAT_SCOPE=1"


# mpi profiler should be preloaded using the following variable
export LD_PRELOAD=$LDMS_MPI_PROFILER_PATH

### Run command. Change this number to one of the above configuration numbers.
RUN_CONF_NUMBER=2

RUN_CONF=CONF$RUN_CONF_NUMBER
RUN_CONF_MSG=MSG_CONF$RUN_CONF_NUMBER

### Change number of processes here
NUM_PROCESS=4

### Change the run time of the aplication here
LOOP_LENGTH=10000000

COMMAND="${!RUN_CONF} $MPI_RUN_COMMAND -np $NUM_PROCESS $MPI_APP_PATH $LOOP_LENGTH \"$RUN_CONF\""

echo ""
echo ${!RUN_CONF_MSG}
echo ""
echo $COMMAND
echo ""
eval $COMMAND

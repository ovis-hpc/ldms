"""Configuration of ldmsd Tests

This file contains the required variables in the tests.
"""

SAMPLERD_OVIS = "/${HOME}/${HOSTNAME}"
AGG_OVIS = "/${HOME}/${HOSTNAME}"

LOG_HOME = "/var/log/ovis"
SOCK_HOME = "/var/run/ovis"
STORE_HOME = "/etc/store/"

#==================================================================
# DO NOT DELETE/RENAME THE VARIABLES AFTER THIS POINT
#
# TODO for testers: Please assign values to the following variables
#==================================================================

#-------------------------------------------
# BEGIN: Test environment-specific variables
#-------------------------------------------

#=========== Path to the secret word for authentication test
SECRETWORD_FILE = ""

#=========== List of hosts that will run samplerd(s)
SAMPLERD_HOSTS = ["localhost"]

#=========== Transport used by the samplerd(s)
SAMPLERD_XPRT = "sock"

#=========== Listener ports used by the samplerd(s)
SAMPLERD_PORT = 10001

#=========== Log file path of the samplerd(s). Bash string extension is valid
SAMPLERD_LOG = SAMPLERD_OVIS + LOG_HOME + "/ldmsd_sampler.log"

#=========== Socket path of the samplerd(s). Bash string extension is valid
SAMPLERD_SOCK = SAMPLERD_OVIS + SOCK_HOME + "/ldmsd_sampler.sock"

#=========== samplerd's listener ports for inet ctrl configuration
SAMPLERD_INET_CTRL_PORT = 20001

#===========hostnames of aggregators
# "localhost" must be in the list.
AGG_HOSTS = ["localhost"]

#===========Transport used by the aggregators
AGG_XPRT = "sock"

#===========Listener port of the aggregators
AGG_PORT = 10002

#===========Log file path of the aggregators
AGG_LOG = AGG_OVIS + LOG_HOME + "/ldmsd_agg.log"

#===========Socket path of the aggregators
AGG_SOCK = AGG_OVIS + SOCK_HOME + "/ldmsd_agg.sock"

#===========Listener ports for python-remote configuration of the aggregators
AGG_INET_CTRL_PORT = 20002

#===========hostnames of the 2nd-level aggregators
# "localhost" must be in the list.
AGG2_HOSTS = ["localhost"]

#===========Transport used by the 2nd-level aggregators
AGG2_XPRT = "sock"

#===========Listener port of the 2nd-level aggregators
AGG2_PORT = 10003

#===========Log file path of the 2nd-level aggregators
AGG2_LOG = AGG_OVIS + LOG_HOME + "/ldmsd_agg_2nd.log"

#===========Socket path of the 2nd-level aggregators
AGG2_SOCK = AGG_OVIS + SOCK_HOME + "/ldmsd_agg_2nd.sock"

#===========Listener ports for python-remote configuration of the 2nd-level aggregators
AGG2_INET_CTRL_PORT = 20003

#######################
# Stored are ldmsd processes that are for storing only. They don't aggregate
# from samplerd directly. This is for testing the scenarios that
# the aggregators and stored are different ldmsd processes.
#
# Hence, the listener port and the socket file must be *different* from samplerds' and
# aggregators'listener ports and socket files.
#######################

#===========hostnames of the stored
# "localhost" must be in the list.
STORED_HOSTS = ["localhost"]

#===========Transport used by the stored
STORED_XPRT = "sock"

#===========Listener port of the stored
STORED_PORT = 10004

#===========Log file path of the stored
STORED_LOG = AGG_OVIS + LOG_HOME + "/ldmsd_stored.log"

#===========Socket path of the stored
STORED_SOCK = AGG_OVIS + SOCK_HOME + "/ldmsd_stored.sock"

#===========Listener ports for python-remote configuration of the stored
STORED_INET_CTRL_PORT = 20004


#------------------------------------------
# END: Test environment-specific variables
#------------------------------------------


#=========== Number of test instances on each samplerd
NUM_TEST_INSTANCES_PER_HOST = 1

#=========== Prefix-name of a test instance. Hard-coded in ldmsd. DON'T Change.
TEST_INSTANCE_PREFIX_NAME = "test_set"  # Don't change. It is defined in ldmsd.

#=========== Number of metrics in each test instance
TEST_INSTANCE_NUM_METRICS = 50

#=========== Aggregator re-connect interval. Default is 20 seconds, hard-coded in ldmsd.
LDMSD_RECONNECT_INTERVAL = 20 # seconds

#=========== Aggregator re-connect offset.
LDMSD_RECONNECT_OFFSET = 0 # microseconds

#=========== Number of samplerd per aggregator (for large-scale test)
# Number of samplerd that will be added to an aggregator
# If there is only one element 'n', at most 'n' samplerd will be added
# to an aggregator, depending on the number of given samplerd hosts.
#
# If there are more than one elements, each number represents one test case.
NUM_SAMPELRD_PER_AGG = [10000]

#=========== List of store plugins and their configuration
STORE_PATH = AGG_OVIS + STORE_HOME

#-------------------------------------------
# BEGIN: Variables related to ldmsd commands
#-------------------------------------------

#=========== Update interval given when construct add_host command.
LDMSD_UPDATE_INTERVAL = 2 # seconds

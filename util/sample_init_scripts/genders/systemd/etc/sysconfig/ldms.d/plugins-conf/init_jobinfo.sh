# This script fragment runs in the daemon's environment 
# via inclusion at ldmsd startup, if it has been configured.
#
# Creates a 'no job' data file so jobid/jobinfo plugins can run
# quietly on any node, even where slurmd does not.
#
# Assume that job data file is named as indicated in
# job_stubfile. You may need to revise this for your site.
job_stubfile=$localstatedir/run/ldms.slurm.jobinfo
mkdir -p $localstatedir/run
if ! test -f $job_stubfile; then
cat << EOF > $job_stubfile
JOBID=0
UID=0
USER=nobody
JOB_USER=nobody
JOB_USER_ID=0
JOB_ID=0
JOB_APP_ID=0
JOB_END=0
JOB_EXIT=0
JOB_NAME=none
JOB_START=0
JOB_STATUS=0
EOF
fi

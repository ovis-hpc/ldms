#!/bin/bash

source /home/brandt/ldms_sample_scripts/ldms_env

sockname=/tmp/run/ldmsd/ldmsd_sock
cfg=/tmp/opt/ovis/agg_conf/$1

ldmsd_controller --sockname ${sockname} --source ${cfg}

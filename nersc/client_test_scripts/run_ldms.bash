#!/bin/bash
set -x
/opt/ovis-ldms/sbin/ldmsd -x sock:6002 -c /opt/ovis-ldms/etc/ldms/nersc-ldmsd.sampler.generated.conf -a munge -v INFO -m 512K -r /opt/ovis-ldms/var/run/ldmsd/sampler.pid

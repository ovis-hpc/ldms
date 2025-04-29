#!/bin/bash

set -e
set -x

D=$( dirname $0 )
SRCDIR=$( realpath ${D}/../../ )

mkdir -p /test/logs
pushd /test/
ln -fs ${SRCDIR}/.github/compat/list_samp.py
ln -fs ${SRCDIR}/.github/compat/list_samp.sh

ln -fs /opt/ovis-4/sbin/ldmsd /opt/ovis-4/sbin/ldmsd-4

cat > samp-4.3.3.conf << EOF
load name=meminfo
config name=meminfo producer=ovis-4.3.3 instance=ovis-4.3.3/meminfo
start name=meminfo interval=1000000 offset=0
EOF

cat > agg-4.3.3.conf << EOF
prdcr_add name=samp type=active host=localhost port=10000 xprt=sock interval=1000000
prdcr_start name=samp
updtr_add name=updtr interval=1000000 offset=100000
updtr_prdcr_add name=updtr regex=.*
updtr_start name=updtr
EOF

cat > agg-4.conf << EOF
prdcr_add name=samp type=active host=127.0.0.1 port=10001 xprt=sock interval=1000000
prdcr_start name=samp
updtr_add name=updtr interval=1000000 offset=200000
updtr_prdcr_add name=updtr regex=.*
updtr_start name=updtr
EOF

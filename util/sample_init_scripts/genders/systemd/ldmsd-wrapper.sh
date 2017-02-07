#! /bin/bash
export LD_LIBRARY_PATH=$LDMS_PREFIX/lib:/usr/lib:$LD_LIBRARY_PATH
mkdir -p $LDMS_PREFIX/var/run/ldmsd/tmp
if test -n "$LDMS_PREFIX"; then
	inst="$LDMS_PREFIX"
else
	inst=/usr
fi
echo "$NUMACTL $NUMAOPT $inst/sbin/ldmsd $*" > $LDMS_PREFIX/var/run/ldmsd/tmp/ldmsd.start
$NUMACTL $NUMAOPT $inst/sbin/ldmsd $*
#LDMS_POST_INSTALLED=0 do not change this line

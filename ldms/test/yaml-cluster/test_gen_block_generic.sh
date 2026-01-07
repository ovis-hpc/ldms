#! /bin/bash
# before running, path and pythonpath must include
# PYTHONPATH=$prefix/lib/$pyver/site-packages
# PATH=$prefix/bin:$prefix/sbin:$PATH

# partial test for at least python3:
if test "x$(which python3)" = "x/usr/bin/python3"; then
       	:
else
       	echo "requires /usr/bin/python3 in path first"
	exit 1
fi

# set logging and outing to /bin/true to use new options
LOGGING=/bin/false
OUTING=/bin/false

# set input and expected group output count
input=./block_generic.yaml
EXPECTED=1540
#input=./large_generic.yaml
#EXPECTED=31540

odir=./output-generic
logroot=./log-generic
gdir=$odir/groups
adir=$odir/agg-daemons
ldir=$odir/local-daemons
sdir=$odir/some-daemons
loggdir=$logroot/groups
logadir=$logroot/agg-daemons
logldir=$logroot/local-daemons
logsdir=$logroot/some-daemons

TIME="/usr/bin/time -f %es"
echo cleaning old files
/bin/rm -rf $odir $logroot
mkdir -p $gdir $odir $sdir $ldir $adir
mkdir -p $loggdir $logdir $logsdir $logldir $logadir
echo done
# maxd controls the test loop compute node count, not the yaml cluster definition.
MAXD=10
some="cn2,cnadmin2,cngw2,cluster-login2,cnadmin2-agg"
samplers="cn[1-$MAXD],cngw[1-24],cluster-login[1-12],cnadmin[1-8]"
agg="cnadmin[1-8]-agg"
echo "generating group files in $gdir, logs in $loggdir"
echo run: ldmsd_yaml_parser --ldms_config $input --generate_config_path $gdir
if $LOGGING; then
	LOG="-l debug -L $loggdir/err"
fi
if $OUTING; then
	OUT="> $loggdir/log"
fi
$TIME ldmsd_yaml_parser --ldms_config $input --generate_config_path $gdir $LOG $OUT
dcount=$(ls $gdir/* |wc -l)
if ! test "x$dcount" = "x$EXPECTED"; then
	echo "daemon files are missing in $gdir ; --generate_config_path test FAIL"
	exit 1
else
	echo "found expected $EXPECTED files in $gdir."
fi

echo "generating single files in $sdir, logs in $logsdir"
date
for i in $(hostlist -d" " -e $some); do
	if $LOGGING; then
		LOG="-l info -L $logsdir/$i.err"
	fi
	if $OUTING; then
		OUT="-o $sdir/$i.conf > $logsdir/$i.log"
	else
		OUT="> $sdir/$i.conf"
	fi
	eval $TIME ldmsd_yaml_parser --ldms_config $input --daemon_name $i $LOG $OUT
	if test -f $sdir/$i.conf; then
		echo for $i $(wc -l $sdir/$i.conf)
	else
		echo $i FAILED
		exit 1
	fi
done
echo "generating single agg files in $adir, logs in $logadir"
date
for i in $(hostlist -d" " -e $agg); do
	if $LOGGING; then
		LOG="-l info -L $logadir/$i.err"
	fi
	if $OUTING; then
		OUT="-o $adir/$i.conf > $logadir/$i.log"
	else
		OUT="> $adir/$i.conf"
	fi
	eval $TIME ldmsd_yaml_parser --ldms_config $input --daemon_name $i $LOG $OUT
	if test -f $adir/$i.conf; then
		echo for $i $(wc -l $adir/$i.conf)
	else
		echo $i FAILED
		exit 1
	fi
done
echo "generating sampler single files in $ldir, logs in $logldir"
date
for i in $(hostlist -d" " -e $samplers); do
	if $LOGGING; then
		LOG="-l info -L $logldir/$i.err"
	fi
	if $OUTING; then
		OUT="-o $ldir/$i.conf > $logldir/$i.log"
	else
		OUT="> $ldir/$i.conf"
	fi
	eval $TIME ldmsd_yaml_parser --ldms_config $input --daemon_name $i $LOG $OUT
	if test -f $ldir/$i.conf; then
		echo for $i $(wc -l $ldir/$i.conf)
	else
		echo $i FAILED
		exit 1
	fi
done
date

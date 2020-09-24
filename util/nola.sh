#!/bin/sh
# use the la files found under the current dir to delete those under $prefix/lib
prefix=$1
files=`find . -name '*.la'`
if ! test -d $1; then
	echo "target dir not found"
	exit 1
fi
x=""
for i in $files; do
	j=`basename $i`
	find $1 -name $j -print -exec /bin/rm {} \;
done

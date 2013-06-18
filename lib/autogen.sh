#!/bin/sh

# create config dir if not exist
test -d config || mkdir config

set -x
aclocal -I config
# On MAC OS X, GNU libtoolize is named 'glibtoolize':
if [ `(uname -s) 2>/dev/null` = 'Darwin' ]
then
	LIBTOOLIZE=glibtoolize
else
	LIBTOOLIZE=libtoolize
fi
${LIBTOOLIZE} --force --copy
autoheader
automake --foreign --add-missing --copy
autoconf

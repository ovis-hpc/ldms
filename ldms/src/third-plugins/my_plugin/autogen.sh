#! /bin/sh
# customize the next line
required=src/my_plugin.c

# below here is boilerplate.
# expected are automake 1.13.4, autoconf 2.69 and libtool 2.4.2 for redhat 7.
# expected is gettext-devel or equivalent for disable rpath support.
if test -f $required ; then
	autoreconf --force --install -v -I m4 -i
else
	echo "this script for regenerating build system for plugin. can't find file $required"
	exit 1
fi

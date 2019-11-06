#!/bin/bash

## This file captures your own developer build; it is an example, not a requirement.
## It just points (as you set it) to an arbitrary ldms installation
## that may not exist on any host but your development machine.

#############################################
# section 1: hunt up the ldmsd installation configvars files

### find the ldmsd installation info and load it if not given variables
### LDMS_CONFIG_SH and LIB_CONFIG_SH in the environment.

# try the ldmsd in the path
base=$(which ldmsd)
if test -n "$base"; then
	guessprefix=$(dirname $(dirname $base))
	echo "prefix from path ldmsd: $guessprefix"
fi
if ! test -f "$LDMS_CONFIG_SH"; then
	echo "LDMS_CONFIG_SH not given."
	if test -z "$guessprefix"; then
		echo "no ldmsd in PATH to guess from."
		exit 1
	fi
	for lib in lib64 lib; do
		if test -f $guessprefix/$lib/ovis-ldms-configvars.sh; then
			LDMS_CONFIG_SH=$guessprefix/$lib/ovis-ldms-configvars.sh
			echo assuming LDMS_CONFIG_SH file from installation at $guessprefix/$lib
		fi
	done
fi
if ! test -f "$LIB_CONFIG_SH"; then
	echo LIB_CONFIG_SH not given. 
	if test -z "$guessprefix"; then
		echo no ldmsd in PATH to guess from.
		exit 1
	fi
	for lib in lib64 lib; do
		if test -f $guessprefix/$lib/ovis-lib-configvars.sh; then
			LIB_CONFIG_SH=$guessprefix/$lib/ovis-lib-configvars.sh
			echo assuming LIB_CONFIG_SH file from installation at $guessprefix/$lib
		fi
	done
fi

if ! test -f "$LDMS_CONFIG_SH"; then
	echo LDMS_CONFIG_SH not set or defaulted. quitting plugin at $(pwd).
	exit 1
fi
if ! test -f "$LIB_CONFIG_SH"; then
	echo LIB_CONFIG_SH not set or defaulted. quitting plugin at $(pwd).
	exit 1
fi

# load shell variables to local shell.
# pass them to configure or re-export them as needed.
. $LIB_CONFIG_SH
. $LDMS_CONFIG_SH
ldms_configure_args=$(cat $ovis_ldms_pkglibdir/ovis-ldms-configure-args)
# note that config args output quoting is from extended from autoconf and may need additional massage before reuse.
# There is a difference between:
# eval echo $(cat $ovis_ldms_pkglibdir/ovis-ldms-configure-args)
# and
# echo $ldms_configure_args

# check and fetch configure args from build
if ! test -f $ovis_ldms_includedir/ovis-ldms-config.h; then
	echo "missing $ovis_ldms_includedir/ovis-ldms-config.h. Installing devel package needed?"
	exit 1
fi

ldms_package_version=$(grep 'define OVIS_LDMS_PACKAGE_VERSION' $ovis_ldms_includedir/ovis-ldms-config.h | sed -e 's/[^"]*//' -e s/\"//g)

### This concludes section 1.
#############################################

### The next section is for an autotools project.
### It would be different for a cmake project, obviously.

# finally, build it from scratch.
if ! test -f configure || test ./configure -ot Makefile.am; then
	./autogen.sh
fi
if ! test -f configure; then
	echo autogen did not create configure
	exit 1
fi

rm -rf plug_obj
mkdir -p plug_obj
cd plug_obj ; # perform a VPATH build

# We pass in ovis configure args so that packagers compiling extensions can pass all arguments in at the top.
source $ovis_ldms_pkglibdir/ovis-ldms-configure-env
eval ../configure $(cat $ovis_ldms_pkglibdir/ovis-ldms-configure-args) --prefix=$ovis_ldms_prefix --with-ldms-libdir=$ovis_ldms_libdir --disable-rpath && make && make install

#!/bin/bash

# Given a number of ldms relocatable rpms specified on the command line,
# un-roll them into the current working directory, then run the relocation
# script to fix all paths to be relative to the working directory

# FIXME: if the rpms contain genders support, and if you have a ldms.d test
# directory containing the config files you want to use, then specify the
# path to that directory in GENDERS_CONFIG_DIR, and the script will copy the
# config files into $PWD/etc/sysconfig/ldms.d, e.g.
#	GENDERS_CONFIG_DIR=/home/users/krehm/test-relo/ldms.d.local
# If you also want to use systemd to start the configured ldms, then set
# USE_SYSTEMD to true.  The script will determine the LDMSCLUSTER name by
# examining your $GENDERS_CONFIG_DIR/ClusterSecrets/*.ldmsauth.conf file.
GENDERS_CONFIG_DIR=
USE_SYSTEMD=false

if test -n "$GENDERS_CONFIG_DIR"; then
	if ! test -d "$GENDERS_CONFIG_DIR"; then
		echo "$GENDERS_CONFIG_DIR is not a directory" >&2
		exit 1
	fi
	if ! test -d "$GENDERS_CONFIG_DIR/ClusterSecrets"; then
		echo "$GENDERS_CONFIG_DIR/ClusterSecrets does not exist" >&2
		exit 1
	fi
	LDMSAUTH=$(echo $GENDERS_CONFIG_DIR/ClusterSecrets/*.ldmsauth.conf)
	if test -z "$LDMSAUTH" -o ! -f "$LDMSAUTH"; then
		echo "$GENDERS_CONFIG_DIR/ClusterSecrets does not contain a ldmsauth.conf file " >&2
		exit 1
	fi
fi

# Unpack all the rpms

for rpm in $*
do 
	rpm2cpio $rpm | cpio -idmv 2>/dev/null
done

USR=$PWD/usr
ETC=$PWD/etc
VAR=$PWD/var
RELO_DIR=$USR/share/doc/ovis-*/relocation

# Relocate all the files using full pathname prefixes.

bash $RELO_DIR/relocate-paths.sh $USR $ETC $VAR $RELO_DIR/manifest

# Copy in any provided genders config files.

if test -n "$GENDERS_CONFIG_DIR" ; then
	DIRS="$(cd $GENDERS_CONFIG_DIR; find * -type d)"
	FILES="$(cd $GENDERS_CONFIG_DIR; find * -type f)"

	DST=$ETC/sysconfig/ldms.d

	for dir in $DIRS
	do
		mkdir -p $DST/$dir
	done
	for file in $FILES
	do
		cp $GENDERS_CONFIG_DIR/$file $DST/$file
	done

	# Place the systemd link if so requested

	if test "$USE_SYSTEMD" = "true"; then
		LDMSAUTH=$(echo $GENDERS_CONFIG_DIR/ClusterSecrets/*.ldmsauth.conf)
		LDMSAUTH=${LDMSAUTH##*/}
		LDMSAUTH=${LDMSAUTH%.ldmsauth.conf}

		if test "$LDMSAUTH" = "local"; then
			SERVICE=ldmsd.service
		else
			SERVICE=ldmsd@.service
		fi
		sudo rm -f /etc/systemd/system/$SERVICE
		sudo ln -s $USR/lib/systemd/system/$SERVICE /etc/systemd/system/$SERVICE
		sudo systemctl daemon-reload
	fi
fi

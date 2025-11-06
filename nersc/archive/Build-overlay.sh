#!/bin/bash
#
# README
#
# The aries libgpcd sampler needs the libgpcd library which is not
# a module installed on the system.
#
# The format ofthe option is as follows:
# --with-aries-libgpcd=LIBDIR,INCDIR for aries-mmr
#
RELTIME=$(date +%Y%m%d%H%M%S)
RELNAME=""
REL=${RELTIME}${RELNAME}

ARIES_LIBGPCD=/opt/cray/gni/default/lib64,/opt/cray/gni/default/include/gpcd
PLATFORM=TRINITY

# Find ovis version
. ./build.env
OVIS_SRC=${PWD}/${BASE}

CFLAGS='-g -O'

# Exit immediately if a command failed
set -e

ARCH=$(uname -m)

RPMBUILD=$PWD/rpmbuild
rm -rf $RPMBUILD
mkdir -p $RPMBUILD/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

TMP_ROOT=$PWD/tmproot
rm -rf $TMP_ROOT

TMP_ROOT_PREFIX=$TMP_ROOT/opt/ovis
mkdir -p $TMP_ROOT_PREFIX

WITH_OVIS_LIB="--with-ovis-lib=$TMP_ROOT_PREFIX"
WITH_SOS="--with-sos=/opt/ovis"
WITH_SLURM="--with-slurm=/opt/slurm"

WITH="$WITH_OVIS_LIB $WITH_SOS $WITH_SLURM"

function pkg_name() {
	case $1 in
	lib)
		echo -n "ovis-lib"
	;;
	ldms)
		echo -n "ovis-ldms"
	;;
	sos)
		echo -n "sosdb"
	;;
	*)
		exit -1
	;;
	esac
}

function spec_name() {
	echo -n $(pkg_name $1).spec
}

LIST="ldms"
for X in $LIST; do
	set -x; # enable command echo

	cp *.tar.gz $RPMBUILD/SOURCES

	NAME=$(basename $X)
	SPEC=$(spec_name ${NAME})
	cp $SPEC $RPMBUILD/SPECS
	rpmbuild --define "_topdir $RPMBUILD" \
		--define "ovis_base ${BASE}" \
		--define "ovis_base_tarball ${BASE_TARBALL}" \
        	--define "_with_sos ${WITH_SOS}" \
		--define "_with_ovis_lib $TMP_ROOT_PREFIX" \
		--define "_with_slurm /opt/slurm" \
		--define "_with_aries_libgpcd $ARIES_LIBGPCD" \
		--define "_with_rca /opt/cray/rca/default" \
		--define "_with_krca /opt/cray/krca/default" \
		--define "_with_cray_hss_devel /opt/cray-hss-devel/default" \
		--define "rel $REL" \
		-ba $RPMBUILD/SPECS/$SPEC

	case $X in
	lib)
		RPM_PTN="ovis-lib"
	;;
	sos)
		RPM_PTN="sosdb"
	;;
	*)
		RPM_PTN=""
	;;
	esac
	if test -n "$RPM_PTN"; then
		# install ovis-lib and sosdb as build prerequisite of ldms
		pushd $TMP_ROOT
		for R in $RPMBUILD/RPMS/$ARCH/${RPM_PTN}-*.rpm; do
			rpm2cpio $R | cpio -dium
		done
		popd
	fi

	set +x; # disable command echo so that it won't print the "for ..." command
	echo "----- DONE -----"
done

echo "FINISH: Please find RPMS and SRPMS in ./rpmbuild/RPMS and ./rpmbuild/SRPMS respectively"

if [ -z "$SCRATCH" ]; then
    echo 'No $SCRATCH directory defined, skipping copy to SCRATCH.'
else
    rm -rf $SCRATCH/ovis-rpms/RPMS $SCRATCH/ovis-rpms/SRPMS
    cp -r ./rpmbuild/RPMS ./rpmbuild/SRPMS $SCRATCH/ovis-rpms
cat - <<END
To copy these RPMs back to gertsmw please run

rsync -va -e ~roman/bin/nidssh mom1:$SCRATCH/ovis-rpms/ /home/roman/src/nersc-zypper/nersc-ovis

END
fi

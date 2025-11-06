#!/bin/bash

set -x
IMPS_IMAGE=nersc-elogin-production-cle_7.0.up03_sles_15sp2-x86_64_ari-20230312

MOUNT_POINTS=/mnt
IMPS_IMAGE_ROOTS=/var/opt/cray/imps/image_roots/

BUILD_OVERLAY=ldms-build-overlay
BUILD_UPPER=ldms-build-upper
BUILD_IMAGE_ROOT=ldms-build-imps-image-root

IMPS_IMAGE_PATH=${IMPS_IMAGE_ROOTS}/${IMPS_IMAGE}
BUILD_UPPER_IMG=/tmp/${BUILD_UPPER}.img
BUILD_OVERLAY_MOUNT_POINT=${MOUNT_POINTS}/${BUILD_OVERLAY}
BUILD_LOWER_MOUNT_POINT=${MOUNT_POINTS}/${BUILD_IMAGE_ROOT}
BUILD_UPPER_MOUNT_POINT=${MOUNT_POINTS}/${BUILD_UPPER}
OVERLAY_WORK_DIR=/root
BUILD_OVERLAY_WORK_DIR=${BUILD_OVERLAY_MOUNT_POINT}/${OVERLAY_WORK_DIR}

UPPER_LOOPDEV=""

NOW="$(date +%Y%m%d%H%M%S)"

# assumes that this script is in the nersc subdirectory of the ovis sources
get_ldms_srcdir() {
    echo "$(readlink -f $(dirname $0))""/.."
}

x_mkdir() {
    mkdir "$1"
    if [ $? -ne 0 ]; then
        echo Unable to create mount point "$1"
        exit 1
    fi
}

setup_upper() {
    local upper_image=$1
    local upper_mount_point=$2

    # Create mount point for the upper file system
    x_mkdir ${upper_mount_point}

    # Create a loopback device for the upper file system
    if [ -e ${upper_image} ]; then
        echo Filesystem image ${upper_image} exists, aborting.
        exit 1
    fi

    # Create the image file
    dd if=/dev/zero of=${upper_image} bs=1024k count=1024
    if [ $? -ne 0 ]; then
        echo Unable to create filesystem image ${upper_image}
        exit 1
    fi

    # Assign the image to a block device
    local loopdev=$(losetup --find --show ${upper_image})
    if [ $? -ne 0 -o -z "$loopdev" ]; then
        exit 1
    fi

    # Create an ext2 filesystem
    mkfs -t ext2 ${loopdev}
    if [ $? -ne 0 ]; then
        echo mkfs on device ${loopdev} failed, aborting.
        exit 1
    fi

    # Now mount the device
    mount $loopdev ${upper_mount_point}
    if [ $? -ne 0 ]; then
        echo Unable to mount ${loopdev} on ${upper_mount_point}
        exit 1
    fi

    # Create upperdir and workdir
    x_mkdir ${upper_mount_point}/upperdir
    x_mkdir ${upper_mount_point}/workdir

    # Return name of loop device in UPPER_LOOPDEV
    UPPER_LOOPDEV=${loopdev}
}

setup_image_root() {
    local imps_image_path=$1
    local image_mount_point=$2

    # Image root for build
    x_mkdir ${image_mount_point}

    # Mount the image root
    mount --bind ${imps_image_path} ${image_mount_point}
    if [ $? -ne 0 ]; then
        echo Unable to mount ${imps_image_path} on ${image_mount_point}
        exit 1
    fi
}

setup_overlay() {
    local lower_mount_point=$1
    local upper_mount_point=$2
    local overlay_mount_point=$3

    # Mount point for the overlay filesystem
    x_mkdir ${overlay_mount_point}

    upperdir=${upper_mount_point}/upperdir
    workdir=${upper_mount_point}/workdir

    # Mount the overlay filesystem
    mount -t overlay overlay -o lowerdir=${lower_mount_point},upperdir=${upperdir},workdir=${workdir} ${overlay_mount_point}
}

teardown_mount() {
    # unmounts the file system mounted at $1 and removes the directory $1
    local mount_point=$1

    umount ${mount_point}
    if [ $? -ne 0 ]; then
        echo Failed to unmount ${mount_point}
        exit 1
    fi

    rmdir ${mount_point}
    if [ $? -ne 0 ]; then
        echo Failed to remove directory ${mount_point}
        exit 1
    fi
}

teardown_overlay() {
    teardown_mount $1
}

teardown_upper() {
    teardown_mount $1

    if [ -z "${UPPER_LOOPDEV}" ]; then
        echo No loopback device was defined
        echo To see the list of existing devices run:
        echo     losetup --list
        echo
        exit 1
    fi

    losetup --detach ${UPPER_LOOPDEV}
    if [ $? -ne 0 ]; then
        echo Aborting: failed to detach ${UPPER_LOOPDEV}
        exit 1
    fi

    rm ${BUILD_UPPER_IMG}
    if [ $? -ne 0 ]; then
        echo Aborting: Unable to remove image file ${BUILD_UPPER_IMG}
        exit 1
    fi
}

teardown_image_root() {
    teardown_mount $1
}

setup() {
    setup_image_root ${IMPS_IMAGE_PATH} ${BUILD_LOWER_MOUNT_POINT}
    setup_upper ${BUILD_UPPER_IMG} ${BUILD_UPPER_MOUNT_POINT}
    setup_overlay ${BUILD_LOWER_MOUNT_POINT} ${BUILD_UPPER_MOUNT_POINT} ${BUILD_OVERLAY_MOUNT_POINT}
    setup_chroot ${BUILD_OVERLAY_WORK_DIR}
}

teardown() {
    teardown_overlay ${BUILD_OVERLAY_MOUNT_POINT}
    teardown_upper ${BUILD_UPPER_MOUNT_POINT}
    teardown_image_root ${BUILD_LOWER_MOUNT_POINT}
}

read_config_var() {
    local config_h="${1}"
    local var="${2}"
    perl -ane '{print "$1\n" if /^#define '"${var}"' \"(.*)\"/}' ${config_h}
}

get_config_var() {
    local config_h="$1"
    local var="$(read_config_var ${config_h} $2)"
    if [ -z "${var}" ]; then
        echo "Could not find value for ${var}"
        exit 1
    fi
    echo "${var}"
}

get_ovis_version() {
    echo "$(get_config_var $1 VERSION)"
}

get_tarball_base() {
    # Derive name of tarball from configure variables
    local config_h="$1"

    # logic and variable names match those in the Makefile
    local PACKAGE_NAME="$(get_config_var ${config_h} PACKAGE_NAME)"
    local PACKAGE_VERSION="$(get_config_var ${config_h} PACKAGE_VERSION)"
    local BASE="${PACKAGE_NAME}-${PACKAGE_VERSION}"
    echo ${BASE}
}

setup_chroot() {
    local work_dir="$1"
    local ldms_srcdir="$(get_ldms_srcdir)"

    if [ -z "${ldms_srcdir}" -o ! -d "${ldms_srcdir}" ]; then
        echo Could not identify ldms source directory
        exit 1
    fi

    local ldms_srcdir_owner=$(stat --format=%U ${ldms_srcdir})
    if [ $? -ne 0 -o -z "${ldms_srcdir_owner}" ]; then
        echo Could not determine owner of ${ldms_srcdir}
        exit 1
    fi


    ( cd ${ldms_srcdir} && sudo -u ${ldms_srcdir_owner} ./autogen.sh )
    if [ $? -ne 0 ]; then
        echo Failed to run autogen.sh in LDMS git checkout
        exit 1
    fi

    local ldms_dist_builddir=$(mktemp -d)
    if [ $? -ne 0 -o -z "${ldms_dist_builddir}" -o ! -d "${ldms_dist_builddir}" ]; then
        echo Could not create temporary directory
        exit 1
    fi

    ( cd ${ldms_dist_builddir} && ${ldms_srcdir}/configure )
    if [ $? -ne 0 ]; then
        echo Unable to configure ovis from git for make dist
        exit 1
    fi

    ( cd ${ldms_dist_builddir} && make dist )
    if [ $? -ne 0 ]; then
        echo Unable to build distribution tarball for chroot build
        exit 1
    fi

    local config_h="${ldms_dist_builddir}/config.h"
    if [ ! -e ${config_h} ]; then
        echo Could not find config.h
        exit 1
    fi

    local BASE=$(get_tarball_base ${config_h})
    local BASE_TARBALL="${BASE}.tar.gz"

    local tarball="${ldms_dist_builddir}/${BASE_TARBALL}"
    if [ ! -e "${tarball}" ]; then
        echo Could not find "${tarball}"
        exit 1
    fi

    cp "${tarball}" ${ldms_srcdir}/nersc/*.spec ${ldms_srcdir}/nersc/Build-overlay.sh ${ldms_srcdir}/nersc/in_chroot.sh ${work_dir}
    if [ $? -ne 0 ]; then
        echo Failed to copy files into work directory in overlay image
        exit 1
    fi

    build_env="${work_dir}/build.env"
    echo "# Generated at ${NOW}" > ${build_env}
    echo "BASE=${BASE}" > ${build_env}
    echo "BASE_TARBALL=${BASE_TARBALL}" >> ${build_env}

    rm -rf "${ldms_dist_builddir}"
}

build_in_chroot() {
    local newroot=${1}
    chroot ${newroot} ${OVERLAY_WORK_DIR}/in_chroot.sh
    if [ $? -ne 0 ]; then
        echo Build in chroot failed.  Aborting.
        exit 1
    fi
}

copy_rpms() {
    local src=$1
    local dest=$2
    if [ ! -d  ${dest} ]; then
        x_mkdir ${dest}
        if [ $? -ne 0 ]; then
            echo Failed to create RPM destination directory, aborting
            exit 1
        fi
    fi

    cp -r ${src}/RPMS ${src}/SRPMS ${dest}
    if [ $? -ne 0 ]; then
        echo Unable to copy rpm files to destination directory, aborting.
    fi
}

find_attached_loopdev() {
    losetup | perl -ane '{print "$F[0]\n" if $F[5] eq "'${BUILD_UPPER_IMG}'";}'
}

cleanup() {
    umount ${BUILD_OVERLAY_MOUNT_POINT}
    umount ${BUILD_LOWER_MOUNT_POINT}
    umount -d ${BUILD_UPPER_MOUNT_POINT}
    rmdir ${BUILD_OVERLAY_MOUNT_POINT}
    rmdir ${BUILD_LOWER_MOUNT_POINT}
    rmdir ${BUILD_UPPER_MOUNT_POINT}

    rm ${BUILD_UPPER_IMG}

    local attached_loopdev=$(find_attached_loopdev)
    if [ ! -z "${attached_loopdev}" ]; then
        losetup -d ${attached_loopdev}
    fi
}

case "${1}" in
    'all')
        setup
        build_in_chroot ${BUILD_OVERLAY_MOUNT_POINT}
        copy_rpms ${BUILD_OVERLAY_WORK_DIR}/rpmbuild rpms
        teardown
    ;;
    'setup')
        setup
    ;;
    'build')
        build_in_chroot ${BUILD_OVERLAY_MOUNT_POINT}
    ;;
    'copy')
        copy_rpms ${BUILD_OVERLAY_WORK_DIR}/rpmbuild rpms
    ;;
    'teardown')
        teardown
    ;;
    'cleanup')
        cleanup
    ;;
    *)
    echo "Usage: $0 all|setup|build|copy|teardown"
    exit 1
esac

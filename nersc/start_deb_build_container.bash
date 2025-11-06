#!/bin/bash

# Original
#podman run -it --rm --mount type=bind,source=/home/jstile/software/git/nersc-ovis,target=/builds/nersc/csg/ovis  registry.nersc.gov/csg/ovis-opensuse-leap:15.2-build /bin/bash
# registry.nersc.gov/csg/slurm_build_cos_2.4_ss11:2023-03-31 \
#  ldms_build:lastest \
if [ -z "$LDMS_REPO" ] ; then
    echo "Set repo path and try again: 
export LDMS_REPO=~/software/git/nersc-ovis
export NERSC_ZYPPER_REPO=~/software/git/nersc-zypper
"
    exit 1
fi
export LDMS_REPO="$(readlink -f "$LDMS_REPO")"
#if [ ! -d "~/tmp" ]; then
#  echo "Make tmp"
#  mkdir -p ~/tmp;
#fi
#podman image list |grep slurm_build_cos |grep 'ldms-builder'
#if [ $? -ne 0 ]; then
#  echo "Pull image"
#  TMPDIR=~/tmp podman pull registry.nersc.gov/csg/ldms-builder_cos_2.4_ss11:2023-05-19
#fi

#podman build . -f Dockerfile.ubuntu:22.04 --tag ubuntu_ldmsd_build:1.0.0

echo "Start build container. From there: pushd /builds/nersc/csg/ovis/ && ./nersc/test_build.ubuntu.bash"
podman run -it --rm \
  --mount type=bind,source=$LDMS_REPO,target=/builds/nersc/csg/ovis \
  ubuntu:22.04 \
  /bin/bash



#!/bin/bash

echo "Install Build Dependent RPMS"
SSHOST_DIR="slingshot-host-software-1.7.3-56-sle15-sp2"
SSHOST_PKG="/var/nerscmgr/software/slingshot/1.7.3a/SS_1.7.3a_RC1/${SSHOST_DIR}.tar.gz"
DEP_PKGS_DIR="DEP_PKGS"
if [ -d "$DEP_PKGS_DIR" ]; then
  rm -rf "$DEP_PKGS_DIR" 
fi
mkdir "$DEP_PKGS_DIR"
tar -xzvp -C "$DEP_PKGS_DIR" -f "${SSHOST_PKG}" \
  ${SSHOST_DIR}/rpms/cassini/sle15-sp2/ncn/cray-cxi-driver-devel-0.9-3.1__gcbbeae4.SSHOT2.0.0.x86_64.rpm \
  ${SSHOST_DIR}/rpms/cassini/sle15-sp2/ncn/cray-libcxi-devel-0.9-SSHOT2.0.0_20220609110530_476ebe6.x86_64.rpm \
  ${SSHOST_DIR}/rpms/cassini/sle15-sp2/ncn/cray-slingshot-base-link-devel-0.9-13.1__g82c33fb.SSHOT2.0.0.x86_64.rpm \
  ${SSHOST_DIR}/rpms/cassini/sle15-sp2/ncn/cray-cassini-headers-user-1.0-SSHOT2.0.0_20220519085257_523eca8.x86_64.rpm \
  ${SSHOST_DIR}/rpms/cassini/sle15-sp2/ncn/cray-libcxi-0.9-SSHOT2.0.0_20220609110530_476ebe6.x86_64.rpm \
  --strip-components=5

echo "Build"
podman build . -f nersc/Dockerfile.ci --tag registry.nersc.gov/csg/ovis-opensuse-leap:15.2-build

echo "List tags in registry"
podman search --list-tags registry.nersc.gov/csg/ovis-opensuse-leap
if [ $? -ne 0 ]; then
  echo "Login to registry.nersc.gov"
  podman login registry.nersc.gov --username "${USER}"
  echo "List tags in registry"
  podman search --list-tags registry.nersc.gov/csg/ovis-opensuse-leap
fi
echo "Push image to registry"
podman push --format=docker registry.nersc.gov/csg/ovis-opensuse-leap:15.2-build
if [ $? -ne 0 ]; then
  echo "Success"
else 
  echo "Failed"
fi

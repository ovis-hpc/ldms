#!/bin/bash

D=$(dirname $0)
# go to src tree
cd ${D}/../../

set -e
set -x

pwd

BD=build

git config --global --add safe.directory ${PWD}

mkdir -p ${BD}
pushd ${BD}
../configure --prefix=/opt/ovis-4 --enable-etc --enable-munge
make
make install

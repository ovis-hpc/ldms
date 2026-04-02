#!/bin/bash
echo "[>>] configure"
CFLAGS="-g -O" \
./configure \
 --prefix="/opt/ovis-ldms" \
 --with-slurm="/usr/include/slurm" \
 --with-libevent="/opt/ovis-ldms/lib" \
 --disable-mmap \
 --enable-cray-samplers \
 --enable-doc \
 --enable-doc-html \
 --enable-doc-man \
 --enable-etc \
 --enable-genderssystemd \
 --enable-jobinfo-sampler \
 --enable-kgnilnd \
 --enable-lustre \
 --enable-munge \
 --enable-papi \
 --enable-slurm \
 --enable-spank-plugin \
 --enable-swig \
 --enable-sysclassib \
 --enable-tsampler \
 --enable-cxi_sampler \
 --enable-slingshot

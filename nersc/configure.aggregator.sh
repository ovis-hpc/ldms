#!/bin/bash
echo "[>>] configure"
CFLAGS="-g -O2 -D_FORTIFY_SOURCE=1" 
CYTHON=cython3 \
./configure \
 --prefix="/opt/ovis-ldms" \
 --with-slurm="/usr/include/slurm" \
 --with-libevent="/opt/ovis-ldms/lib" \
 --disable-mmap \
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
 --enable-victoriametrics \
 --enable-store-avro-kafka

#!/bin/bash
echo "[>>] configure"
CFLAGS="-ggdb3 -O0" \
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
 --enable-munge \
 --enable-papi \
 --enable-spank-plugin \
 --enable-swig \
 --enable-sysclassib \
 --enable-tsampler \
 --enable-store-avro-kafka

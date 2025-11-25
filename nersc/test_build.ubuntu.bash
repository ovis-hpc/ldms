#!/bin/bash
pkg_file="ovis-ldms.tar.gz"

apt-get update \
    && apt-get install -y \
    automake \
    autotools-dev \
    bison \
    build-essential \
    bzip2 \
    curl \
    dnsutils \
    doxygen \
    flex \
    gcc \
    gettext \
    git \
    gzip \
    hostname \
    libibverbs-dev \
    libjansson-dev \
    libavro-dev \
    libavro23 \
    librdkafka-dev \
    python3-pyverbs \
    python3-docutils \
    librdmacm-dev \
    libcurl4-openssl-dev \
    iputils-ping \
    libmunge-dev \
    jq \
    less \
    libpapi-dev \
    libpfm4 \
    libpfm4-dev \
    libssl3 \
    libssl-dev \
    libtool \
    m4 \
    make \
    openssl \
    papi-tools \
    pkg-config \
    python3 \
    python3-dev \
    python3-pip \
    cython3 \
    rsync \
    ruby \
    traceroute \
    tree \
    vim

echo "[>>] Container BUG. make /var/spool/slurmd"
if [ ! -d "/var/spool/slurmd" ]; then
  mkdir /var/spool/slurmd
fi
#--------------------------------
# CLEAN
#--------------------------------
echo "[>>] Clean previous install"
for i in \
  /app \
  /opt/ovis-ldms \
  /app/etc/ldms \
  /app/etc/systemd/system/ldmsd.kokkos.service \
  /app/etc/systemd/system/ldmsd.aggregator.service \
  /app/etc/systemd/system/ldmsd.sampler.service \
  lib/etc/ld.so.conf.d/ovis-ld-so.conf \
  $pkg_file; do
  if [ -e "$i" ]; then
    echo "[>>]  Delete $i"
    rm -rf "$i"
  fi
done
echo "[>>] Remove old rpm and tar.gz"
rm -rf ovis-ldms.{rpm,tar.gz}
if [ -d "/build/libserde" ]; then
    rm -rf "/build/libserde"
fi
echo "[>>] Clean source tree"
rm -rf .version
make uninstall
make distclean
make clean
make maintainer-clean
echo "[>>] Clean isn't clean. Remove any file with an .in file"
find ./ -name "*.in" |(while read FOO; do base="$(echo $FOO |sed 's/\.in$//g')"; echo "Remove $base"; rm -rf  "$base"; done; )
find ./ -name "*.cache" -delete
#--------------------------------
# BUILD
#--------------------------------
pushd /builds/
echo "Build libserdes" \
    && git clone https://github.com/confluentinc/libserdes.git \
    && cd libserdes/ \
    && ./configure \
    && make \
    && make install \
    && ls -alF /usr/local/lib/libserdes.so

echo "[>>] Get in source dir"
pushd /builds/nersc/csg/ovis/
echo "[--] Hack /usr/include/dcgm_api_export.h"
set -xe
echo "#define DCGM_PUBLIC_API" >> "/usr/include/dcgm_api_export.h"
echo "[--] Build ldms"
echo "[>>] autoreconf"
autoreconf --install
echo "[>>] autogen"
./autogen.sh
echo "[>>] configure"
./nersc/configure.aggregator.sh
echo "[>>] make"
make -j 10
echo "[>>] make install"
make install
echo "[>>] List product"
tree /opt/ovis-ldms
du -sh /opt/ovis-ldms

echo "BUILD DONE"

echo "[>>] Find python3 site packages"
PYTHON_VERSION="$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
PYTHON_SITE_PKGS="$(python3 -m site --user-site)"
echo "PYTHON_VERSION=$PYTHON_VERSION"
echo "PYTHON_SITE_PKGS=$PYTHON_SITE_PKGS"
#--------------------------------
# ASSEBLE PACKAGE TREE
#--------------------------------
echo "[>>] Make package tree staging area: /app"
mkdir -p \
  /app/etc/profile.d \
  /app/etc/sysconfig \
  /app/usr/lib/systemd/system \
  /app/opt \
  /app/${PYTHON_SITE_PKGS} \
  /app/usr/share
echo "[>>] Stage /usr/share/man"
mv /opt/ovis-ldms/share/man /app/usr/share/
echo "[>>] Stage /opt/ovis-ldms"
mv /opt/ovis-ldms /app/opt/
#rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms*/sample_init_scripts/genders/systemd/etc/sysconfig /app/opt/ovis-ldms/etc/
rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms/sample_init_scripts/opt/etc/systemd/ /app/opt/ovis-ldms/etc/
rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms/sample_init_scripts/opt/etc/ldms /app/opt/ovis-ldms/etc/
rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms/sample_init_scripts/opt/etc/systemd /app/opt/ovis-ldms/etc/
echo "[>>] Stage our config"
cp nersc/nersc-ldmsd.sampler.conf          /app/opt/ovis-ldms/etc/ldms/
cp nersc/nersc-ldmsd.sampler.env     /app/opt/ovis-ldms/etc/ldms/
cp nersc/nersc-ldmsd.sampler.service /app/opt/ovis-ldms/etc/systemd/system/
echo "[>>] Stage /etc"
ln -s /opt/ovis-ldms/etc/profile.d/set-ovis-variables.sh /app/etc/profile.d/set-ovis-variables.sh
echo "[>>] Stage /usr/lib/systemd/system"
ln -s /opt/ovis-ldms/etc/systemd/system/nersc-ldmsd.sampler.service /app/usr/lib/systemd/system/nersc-ldmsd.sampler.service
echo "[>>] Stage ${PYTHON_SITE_PKGS}"
ln -s /opt/ovis-ldms/lib/${PYTHON_VERSION}/ldmsd /app/${PYTHON_SITE_PKGS}/
ln -s /opt/ovis-ldms/lib/${PYTHON_VERSION}/ovis_ldms /app/${PYTHON_SITE_PKGS}/
#---NOTE: Skip this: THIS LINK CONFLICTS WITH CRAY ANSIBLE FOR THE SECRET
#ln -s /opt/ovis-ldms/etc/sysconfig/ldms.d                /app/etc/sysconfig/ldms.d
echo "[>>] Bundle"
tar -C /app -czvpf $pkg_file .
echo "[>>] Build Product"
du -sh $pkg_file

echo "[>>] Install fpm"
gem install public_suffix
gem install dotenv
gem install --no-document  fpm
fpm \
  --input-type tar \
  --output-type deb \
  --name ovis-ldms \
  --version 1.0.0 \
  --license "GPLv2 or BSD" \
  --url "https://github.com/ovis-hpc" \
  --description "This package provides the LDMS commands and libraries.\n* ldmsd: the LDMS daemon, which can run as sampler or aggregator (or both).\n* ldms_ls: the tool to list metric information of an ldmsd.\n* ldmsctl: the tool to control an ldmsd." \
  --package ovis-ldms-$(cat .version).x86_64.deb \
  ovis-ldms.tar.gz
echo "[>>] Inspect Package"
dpkg-deb --info ovis-ldms-$(cat .version).x86_64.deb

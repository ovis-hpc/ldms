#!/bin/bash
pkg_file="ovis-ldms-1.0.0-1.x86_64.rpm"

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
  $pkg_file \
  ovis-ldms-*.rpm \
; do
  if [ -e "$i" ]; then
    echo "[>>]  Delete $i"
    rm -rf "$i"
  fi
done
echo "[>>] Remove old rpm and tar.gz"
rm -rf ovis-ldms.{rpm,tar.gz}
echo "[>>] Clean source tree"
rm -rf .version
make uninstall 
make distclean
make clean
make maintainer-clean
echo "[>>] Clean isn't clean. Remove any file with an .in file"
find ./ -name "*.in" |(while read FOO; do base="$(echo $FOO |sed 's/\.in$//g')"; echo "Remove $base"; rm -rf  "$base"; done; )
find ./ -name "*.cache" -delete
echo "[>>] uninstall ovis-ldms"
rpm -qa |grep ovis-ldms
if [ $? -eq 0 ]; then
  rpm -qa |grep nerscjson_client
  if [ $? -eq 0 ]; then
    rpm -e nerscjson_client
  fi
  rpm -e ovis-ldms
fi
#--------------------------------
# BUILD 
#--------------------------------
echo "[>>] Get in source dir"
pushd /builds/nersc/csg/ovis/
#rpm -i datacenter-gpu-manager-2.2.3-1-x86_64.rpm 
set -xe
echo "[>>] Hack cxi_prov_hw.h for HPE Case: 5402129743"
./nersc/fix_headers.cxi_prov_hw.hpe_case_5402129743.bash
echo "[>>] Patch: Add DCGM_PUBLIC_API"
echo "#define DCGM_PUBLIC_API" >> "/usr/include/datacenter-gpu-manager-4/dcgm_api_export.h" #"/usr/include/dcgm_api_export.h"
echo "[--] Build ldms"
echo "[>>] autoreconf"
autoreconf --install
if [ $? -ne 0 ]; then
    echo "[!!] Failed"
    exit 1
fi
echo "[>>] autogen"
./autogen.sh
if [ $? -ne 0 ]; then
    echo "[!!] Failed"
    exit 1
fi
echo "[>>] configure"
./nersc/configure.sh
if [ $? -ne 0 ]; then
    echo "[!!] Failed"
    exit 1
fi
echo "[>>] make"
make -j 10
if [ $? -ne 0 ]; then
    echo "[!!] Failed"
    exit 1
fi
echo "[>>] make install"
make install
if [ $? -ne 0 ]; then
    echo "[!!] Failed"
    exit 1
fi
#--------------------------------
# ASSEBLE PACKAGE TREE
#--------------------------------
echo "[>>] Make RPM staging area: /app"
mkdir -p \
  /app/etc/profile.d \
  /app/etc/sysconfig \
  /app/opt \
  /app/lib/systemd/system \
  /app/usr/lib/systemd/system \
  /app/usr/lib/python3.6/site-packages \
  /app/usr/share
echo "[>>] Stage /usr/share/man"
mv /opt/ovis-ldms/share/man /app/usr/share/
echo "[>>] Stage /opt/ovis-ldms for RPM"
mv /opt/ovis-ldms /app/opt/
systemd_path="$(find /app/opt/ovis-ldms/ -iname "systemd")"
rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms/sample_init_scripts/opt/etc/systemd /app/opt/ovis-ldms/etc/
rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms/sample_init_scripts/opt/etc/ldms /app/opt/ovis-ldms/etc/
echo "[>>] Add our config"
cp nersc/nersc-ldmsd.sampler.conf          /app/opt/ovis-ldms/etc/ldms/
cp nersc/nersc-ldmsd.sampler.env     /app/opt/ovis-ldms/etc/ldms/
cp nersc/nersc-ldmsd.sampler.service /app/opt/ovis-ldms/etc/systemd/system/
echo "[>>] Stage /etc for RPM"
ln -s /opt/ovis-ldms/etc/profile.d/set-ovis-variables.sh /app/etc/profile.d/set-ovis-variables.sh
echo "[>>] Stage /usr/lib/systemd/system for RPM"
ln -s /opt/ovis-ldms/etc/systemd/system/nersc-ldmsd.sampler.service /app/usr/lib/systemd/system/nersc-ldmsd.sampler.service
echo "[>>] Stage /usr/lib/python3.6 for RPM"
ln -s /opt/ovis-ldms/lib/python3.6/ldmsd /app/usr/lib/python3.6/
ln -s /opt/ovis-ldms/lib/python3.6/ovis_ldms /app/usr/lib/python3.6/
#---NOTE: Skip this: THIS LINK CONFLICTS WITH CRAY ANSIBLE FOR THE SECRET
#ln -s /opt/ovis-ldms/etc/sysconfig/ldms.d                /app/etc/sysconfig/ldms.d
#--------------------------------
# PACKAGE
#--------------------------------
export $(grep PACKAGE_VERSION= ./configure |tr -d "'")
echo "PACKAGE_VERSION=$PACKAGE_VERSION"
pkg_file="ovis-ldms-${PACKAGE_VERSION}.x86_64.rpm"

echo "[>>] Tar up the staged files for the RPM"
tar -C /app -czvpf ovis-ldms.tar.gz .

echo "[>>] install fpm"
#gem install rchardet -v 1.8.0
#gem install public_suffix -v 4.0.7
#gem install dotenv -v 2.8.1
#gem install --no-ri --no-rdoc fpm -v 1.14.2
#gem install fpm -v 1.14.2 -v public_suffix:4.0.7

gem install public_suffix -v 4.0.7
gem install rchardet -v 1.8.0
gem install --no-ri --no-rdoc fpm -v 1.14.2
gem install dotenv -v 2.8.1
gem install --no-ri --no-rdoc fpm -v 1.14.2

fpm.ruby2.5 --version

echo "[>>] Build RPM with fpm"
fpm.ruby2.5 \
--input-type tar \
--output-type rpm \
--name ovis-ldms \
--version $(cat .version) \
--iteration 1 \
--depends bash \
--depends python3-Cython \
--depends python3-devel \
--directories=/opt/ovis-ldms \
--post-uninstall nersc/rpm_postuninstall.txt \
--license "GPLv2 or BSD" \
--rpm-group root \
--description "This package provides the LDMS commands and libraries.\n* ldmsd: the LDMS daemon, which can run as sampler or aggregator (or both).\n* ldms_ls: the tool to list metric information of an ldmsd.\n* ldmsctl: the tool to control an ldmsd." \
--rpm-summary "LDMS - Lighweight Distributed Metric Service" \
--package ovis-ldms-$(cat .version).x86_64.rpm \
ovis-ldms.tar.gz
echo "[>>] List rpm created"
ls -alF "$pkg_file" 
echo "[>>] List files in rpm"
rpm -qlp "$pkg_file"
echo "[>>] Show rpm scripts"
rpm -qp --scripts "$pkg_file" 
echo "[>>] Show rpm info" 
rpm -qi "$pkg_file" 
echo "[--] Success"


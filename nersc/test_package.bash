#!/bin/bash
export $(grep PACKAGE_VERSION= ./configure |tr -d "'")
echo "PACKAGE_VERSION=$PACKAGE_VERSION"
pkg_file="ovis-ldms-${PACKAGE_VERSION}.x86_64.rpm"
  
echo "[>>] Tar up the staged files for the RPM"
tar -C /app -czvpf ovis-ldms.tar.gz .

gem install rchardet -v 1.8.0
gem install --no-ri --no-rdoc fpm -v 1.14.2

echo "[>>] install fpm"
gem install public_suffix -v 4.0.7
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


#!/bin/bash
echo "[>>] Make RPM staging area: /app"
mkdir -p \
  /app/etc/profile.d \
  /app/etc/sysconfig \
  /app/lib/systemd/system \
  /app/opt \
  /app/usr/lib/python3.6/site-packages \
  /app/usr/share
echo "[>>] Stage /usr/share/man"
mv /opt/ovis-ldms/share/man /app/usr/share/
echo "[>>] Stage /opt/ovis-ldms for RPM"
mv /opt/ovis-ldms /app/opt/
rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms*/sample_init_scripts/genders/systemd/etc/sysconfig /app/opt/ovis-ldms/etc/
rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms/sample_init_scripts/opt/etc/ldms /app/opt/ovis-ldms/etc/
rsync -a /app/opt/ovis-ldms/share/doc/ovis-ldms/sample_init_scripts/opt/etc/systemd /app/opt/ovis-ldms/etc/
echo "[>>] Add our config"
cp nersc/nersc-ldmsd.sampler.conf          /app/opt/ovis-ldms/etc/ldms/
cp nersc/nersc-ldmsd.sampler.env     /app/opt/ovis-ldms/etc/ldms/
cp nersc/nersc-ldmsd.sampler.service /app/opt/ovis-ldms/etc/systemd/system/
#echo "[>>] Stage /etc for RPM"
#ln -s /opt/ovis-ldms/etc/profile.d/set-ovis-variables.sh /app/etc/profile.d/set-ovis-variables.sh
echo "[>>] Stage /lib/systemd/system for RPM"
ln -s /opt/ovis-ldms/etc/systemd/system/nersc-ldmsd.sampler.service /app/lib/systemd/system/nersc-ldmsd.sampler.service
echo "[>>] Stage /usr/lib/python3.6 for RPM"
ln -s /opt/ovis-ldms/lib/python3.6/ldmsd /app/usr/lib/python3.6/
ln -s /opt/ovis-ldms/lib/python3.6/ovis_ldms /app/usr/lib/python3.6/
#---NOTE: Skip this: THIS LINK CONFLICTS WITH CRAY ANSIBLE FOR THE SECRET
#ln -s /opt/ovis-ldms/etc/sysconfig/ldms.d                /app/etc/sysconfig/ldms.d

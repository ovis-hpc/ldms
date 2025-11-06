# Prerelease of Cassini LDMS source
----

Deliver: cray-ldms-rpms.cassini_cxi.tar

No documentation on how to use it

```
#
# The tar contains:
#
tar -tvpf  cray-ldms-rpms.cassini_cxi.tar

  -rw-r--r-- jhanson/mppsrc 1314187 2022-07-25 19:44 cray-ldms-3.0.0-2726.1530.220725T2137.a.sles15sp3hpejhanson.src.rpm
  -rw-r--r-- jhanson/mppsrc 1061236 2022-07-25 19:44 cray-ldms-3.0.0-2726.1530.220725T2137.a.sles15sp3hpejhanson.x86_64.rpm
  -rw-r--r-- jhanson/mppsrc   74516 2022-07-25 19:45 cray-ldms-devel-3.0.0-2726.1530.220725T2137.a.sles15sp3hpejhanson.x86_64.rpm
  -rw-r--r-- jhanson/mppsrc   19540 2022-07-25 19:45 cray-ldms-store-kafka-3.0.0-2726.1530.220725T2137.a.sles15sp3hpejhanson.x86_64.rpm

#
# Unpack source package 
#
tar -xvpf cray-ldms-rpms.cassini_cxi.tar cray-ldms-3.0.0-2726.1530.220725T2137.a.sles15sp3hpejhanson.src.rpm

#
# Unpack patches 
#
rpm2cpio cray-ldms-3.0.0-2726.1530.220725T2137.a.sles15sp3hpejhanson.src.rpm | cpio -idmv

  configure.libgenders64.patch
  cray-ldms-3.0.0.tar.bz2
  cray.libcxi.patch
  cray_json.patch
  ldms.spec
  runpath.libgenders64.patch

#
# Extract parts of: cray-ldms-3.0.0.tar.bz2
# It looks like an older version of the ldms source.
# NO idea what the base version was
# Just unpack and commit things that are labeled cray
#
tar -xjvp --strip-components=1 \
 -f cray-ldms-3.0.0.tar.bz2 \
 ldms/ldms/etc/aggregator.conf.cray_xc.example \
 ldms/ldms/etc/ldmsd.aggregator.env.cray_xc.example \
 ldms/ldms/etc/ldmsd.sampler.env.cray_xc.example \
 ldms/ldms/etc/sampler.conf.cray_xc.example \
 ldms/ldms/man/Plugin_cray_dvs_sampler.man \
 ldms/ldms/man/Plugin_cray_system_sampler_variants.man \
 ldms/ldms/src/sampler/cray \
 ldms/ldms/src/sampler/cray_power_sampler.c \
 ldms/ldms/src/sampler/cray_system_sampler \
 ldms/ldms/src/store/cray \
 ldms/util/cray_specific \
 ldms/util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/plugins-conf/cray_gemini_r_sampler \
 ldms/util/sample_init_scripts/genders/systemd/etc/sysconfig/ldms.d/plugins-conf/cray_power_sampler \
 ldms/cray-ldms-shasta.spec

#
# Commit and add 
#

#
# Apply each patch and commit changes
#
patch -p0 < configure.libgenders64.patch ./configure.ac
patch -p1 < cray_json.patch
patch  -p1 < cray.libcxi.patch
manually apply failed runpath.libgenders64.patch
 
#
# Build dependencies
#

Some dev packages are in:  slingshot-host-software-1.7.3-56-sle15-sp3.tar.gz

Need dev packages:

cray-libcxi-devel
cray-cassini-headers-user
cray-cxi-driver-devel
cray-slingshot-base-link-devel

#
# Extract
#
cp /var/nerscmgr/software/slingshot/1.7.3a/SS_1.7.3a_RC1/slingshot-host-software-1.7.3-56-sle15-sp2.tar.gz .

mkdir EXTRACT

tar -xzvp -C EXTRACT -f slingshot-host-software-1.7.3-56-sle15-sp2.tar.gz \
 slingshot-host-software-1.7.3-56-sle15-sp2/rpms/cassini/sle15-sp2/ncn/cray-cxi-driver-devel-0.9-3.1__gcbbeae4.SSHOT2.0.0.x86_64.rpm \
 slingshot-host-software-1.7.3-56-sle15-sp2/rpms/cassini/sle15-sp2/ncn/cray-libcxi-devel-0.9-SSHOT2.0.0_20220609110530_476ebe6.x86_64.rpm \
 slingshot-host-software-1.7.3-56-sle15-sp2/rpms/cassini/sle15-sp2/ncn/cray-slingshot-base-link-devel-0.9-13.1__g82c33fb.SSHOT2.0.0.x86_64.rpm \
 slingshot-host-software-1.7.3-56-sle15-sp2/rpms/cassini/sle15-sp2/ncn/cray-cassini-headers-user-1.0-SSHOT2.0.0_20220519085257_523eca8.x86_64.rpm \
 slingshot-host-software-1.7.3-56-sle15-sp2/rpms/cassini/sle15-sp2/ncn/cray-libcxi-0.9-SSHOT2.0.0_20220609110530_476ebe6.x86_64.rpm \
 --strip-components=5

pushd EXTRACT

rpm -i \
 cray-cassini-headers-user-1.0-SSHOT2.0.0_20220519085257_523eca8.x86_64.rpm \
 cray-libcxi-0.9-SSHOT2.0.0_20220609110530_476ebe6.x86_64.rpm \
 cray-libcxi-devel-0.9-SSHOT2.0.0_20220609110530_476ebe6.x86_64.rpm \
 cray-cxi-driver-devel-0.9-3.1__gcbbeae4.SSHOT2.0.0.x86_64.rpm \
 cray-slingshot-base-link-devel-0.9-13.1__g82c33fb.SSHOT2.0.0.x86_64.rpm

```

I installed these in the CI image




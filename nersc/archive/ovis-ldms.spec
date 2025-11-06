Name: ovis-ldms
Version: 4.3.8
BuildRequires: python3-Cython python3-devel
Release: %{rel}%{?dist}
Summary: LDMS - Lighweight Distributed Metric Service

Group: Applications/System
License: GPLv2 or BSD
URL: https://www.opengridcomputing.com
Source0: %{ovis_base_tarball}

%define _prefix /opt/ovis
%define _sysconfdir /etc%{_prefix}
%define _localstatedir /var%{_prefix}
%define _sharedstatedir %{_localstatedir}/lib
%define _systemdir /usr/lib/systemd/system

%description
This package provides the LDMS commands and libraries.
* ldmsd: the LDMS daemon, which can run as sampler or aggregator (or both).
* ldms_ls: the tool to list metric information of an ldmsd.
* ldmsctl: the tool to control an ldmsd.


%prep
%setup -q -n %{ovis_base}

%build
%configure --enable-etc \
		--enable-influx \
		--enable-munge \
		--enable-swig \
		--enable-ldms-python \
                --enable-ugni \
		--enable-kgnilnd \
                --enable-doc \
                --enable-doc-html \
                --enable-doc-man \
		--enable-sysclassib \
		--enable-lustre \
		--enable-tsampler \
		--enable-cray_power_sampler \
		--enable-cray_system_sampler \
		--disable-gpcdlocal \
		--enable-aries-gpcdr \
		--enable-aries_mmr \
		--enable-aries_linkstatus \
		--enable-jobinfo-slurm \
		--enable-jobinfo-sampler \
		--enable-slurm-sampler \
		--enable-spank-plugin \
		--enable-papi-sampler \
		--enable-syspapi-sampler \
	        %{?_with_sos:--enable-sos}\
                --enable-rabbitv3 \
                --enable-amqp \
		--enable-rdma \
		--disable-mmap \
		--with-slurm=%{_with_slurm} \
		%{?_with_sos:%{_with_sos}} \
		--with-ovis-lib=%{_with_ovis_lib} \
		--with-aries-libgpcd=%{_with_aries_libgpcd} \
		--with-rca=%{_with_rca} \
		--with-krca=%{_with_krca} \
		--with-cray-hss-devel=%{_with_cray_hss_devel} \
		CFLAGS="-g -O" \

# disable rpath when librool re-link
sed -i 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g' libtool
sed -i 's|^runpath_var=LD_RUN_PATH|runpath_var=NO_RUNPATH_PLEASE|g' libtool
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}
# remove unwanted .a and .la files
find %{buildroot} -name '*.a' -exec rm {} \;
find %{buildroot} -name '*.la' -exec rm {} \;

%clean
rm -rf %{buildroot}

# files for main package
%files
%{_bindir}/ldmsd-check-env
%{_bindir}/envldms.sh
%{_bindir}/ldms_ban.sh
%{_bindir}/ldms-l2_test.sh
%{_bindir}/ldms-meminfo.sh
%{_bindir}/ldms-pedigree
%{_bindir}/ldms-plugins.sh
%{_bindir}/ldms-py-subset_test.sh
%{_bindir}/ldms-py-syslog.sh
%{_bindir}/ldms-py-rename.sh
%{_bindir}/ldms-py-varset.sh
%{_bindir}/ldms-static-test.sh
%{_bindir}/ldms_local_opa2test.sh
%{_bindir}/lsdate
%{_bindir}/ovis-roll-over.py
%{_bindir}/ldms_dstat_schema_name
%{_sbindir}
%{_libdir}/libldms.*
%{_libdir}/libldms_auth_naive.*
%{_libdir}/libldms_auth_none.*
%{_libdir}/libldms_auth_ovis.*
%{_libdir}/libldms_auth_munge.*
%{_libdir}/libldmsd_stream.*
%{_libdir}/libldmsd_plugattr.*
%{_libdir}/libldmsd_request.*
%{_libdir}/libsampler_base.*
%{_libdir}/libcoll*
%{_libdir}/libovis_third*
%{_libdir}/libmmalloc*
%{_libdir}/libovis_auth*
%{_libdir}/ovis-ldms/ovis-auth.sh
%{_libdir}/libovis_ctrl.*
%{_libdir}/libovis_event.*
%{_libdir}/libovis_util.*
%{_libdir}/libovis_json.*
%{_libdir}/libovis_ev.*
%{_libdir}/libparse_stat.*
%{_libdir}/libsimple_lps.*
%{_libdir}/libzap.*
%{_libdir}/ovis-ldms/libzap_sock.*
%{_libdir}/ovis-ldms/libzap_rdma.*
%{_libdir}/ovis-ldms/libzap_ugni.*
%config %{_sysconfdir}/ldms/*
%{_sysconfdir}/systemd
%exclude %{_libdir}/ovis-ldms-configvars.sh
%exclude %{_libdir}/ovis-ldms-configvars.sh
%exclude %{_libdir}/ovis-ldms/libstore_flatfile.*
%exclude %{_libdir}/ovis-ldms/libarray_example.*
%exclude %{_libdir}/ovis-ldms/libclock.*
%exclude %{_libdir}/ovis-ldms/libvariable.*
%exclude %{_libdir}/ovis-ldms/ovis-ldms-configure-args
%exclude %{_libdir}/ovis-ldms/ovis-ldms-configure-env

%posttrans
/bin/ln -fs %{_sysconfdir}/systemd/system/ldmsd.aggregator.service %{_systemdir}/ldmsd.aggregator.service
/bin/ln -fs %{_sysconfdir}/systemd/system/ldmsd.sampler.service %{_systemdir}/ldmsd.sampler.service
/usr/bin/systemctl daemon-reload

%post
/bin/rm -f /etc/profile.d/ovis.sh
echo PATH=%{_bindir}:%{_sbindir}:\$PATH > %{_sysconfdir}/ldms/ovis.sh
echo export LDMSD_PLUGIN_LIBPATH=%{_libdir}/ovis-ldms >> %{_sysconfdir}/ldms/ovis.sh
echo export ZAP_LIBPATH=%{_libdir}/ovis-ldms >> %{_sysconfdir}/ldms/ovis.sh
echo export PYTHONPATH=%{_prefix}/lib/python3.6/site-packages >> %{_sysconfdir}/ldms/ovis.sh
/bin/ln -fs %{_sysconfdir}/ldms/ovis.sh /etc/profile.d/ovis.sh
/bin/rm -f %{_sysconfdir}/ldms/ldms-ldd.conf
echo %{_libdir} > %{_sysconfdir}/ldms/ldms-ldd.conf
echo %{_libdir}/ovis-ldms >> %{_sysconfdir}/ldms/ldms-ldd.conf
echo %{_libdir}/ovis-lib >> %{_sysconfdir}/ldms/ldms-ldd.conf
/bin/ln -fs %{_sysconfdir}/ldms/ldms-ldd.conf /etc/ld.so.conf.d/ldms-ldd.conf
/sbin/ldconfig

%preun
/usr/bin/systemctl stop ldmsd.aggregator.service
/usr/bin/systemctl stop ldmsd.sampler.service

%postun
/bin/rm -f %{_systemdir}/ldmsd.aggregator.service
/bin/rm -f %{_systemdir}/ldmsd.sampler.service
/usr/bin/systemctl daemon-reload
/sbin/ldconfig

# ovis-ldms-python package
%package python
Summary: Python tools and interfaces package
Group: Development/Libraries
%description python
Python tools and evelopment interfaces
%files python
%defattr(-,root,root)
%{_bindir}/ldmsd_controller
%{_prefix}/lib*/python*

# ovis-ldms-devel package
%package devel
Summary: Development files for LDMS
Group: Development/Libraries
%description devel
Development files for LDMS
%files devel
%defattr(-,root,root)
%{_includedir}

# ovis-ldms-doc package
%package doc
Summary: LDMS Documentation
Group: Documentation
%description doc
Documentation for LDMS subsystem
%files doc
%defattr(-,root,root)
%{_datadir}/doc
%{_datadir}/man

###################
# sampler plugins #
###################

# ovis-ldms-sampler-jobinfo
%package sampler-jobinfo
Summary: Job Information Sampler
Group: Applications/System
Version: %{version}
%description sampler-jobinfo
%{summary}
%files sampler-jobinfo
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libjobinfo.*
%{_libdir}/ovis-ldms/libjobinfo_slurm.*

# ovis-ldms-sampler-tsampler
%package sampler-tsampler
Summary: High Frequency Sampler Plugins
Group: Applications/System
Version: %{version}
%description sampler-tsampler
%{summary}
%files sampler-tsampler
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libtsampler.*
%{_libdir}/ovis-ldms/libhfclock.*
%{_libdir}/ovis-ldms/libtimer_base.*

# ovis-ldms-sampler-cray-dvs
%package sampler-cray-dvs
Summary: Cray DVS Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-cray-dvs
%{summary}
%files sampler-cray-dvs
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libcray_dvs_sampler.*

# ovis-ldms-sampler-cray-aries
%package sampler-cray-aries
Summary: Cray Aries Sampler Plugins
Group: Applications/System
Version: %{version}
%description sampler-cray-aries
%{summary}
%files sampler-cray-aries
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libaries_linkstatus.*
%{_libdir}/ovis-ldms/libaries_mmr.*
%{_libdir}/ovis-ldms/libaries_nic_mmr.*
%{_libdir}/ovis-ldms/libaries_rtr_mmr.*
%{_libdir}/ovis-ldms/libcray_aries_r_sampler.*
%{_libdir}/ovis-ldms/libaries_mmr_configurable.*
%{_bindir}/check_mmr_configurable

# ovis-ldms-sampler-cray-power
%package sampler-cray-power
Summary: Cray Power Sampler Plugins
Group: Applications/System
Version: %{version}
Requires: ovis-ldms-sampler-tsampler >= %{version}
%description sampler-cray-power
%{summary}
%files sampler-cray-power
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libcray_power_sampler.*

# ovis-ldms-sampler-kgnilnd
%package sampler-kgnilnd
Summary: Cray KGNI LND LDMS Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-kgnilnd
%{summary}
%files sampler-kgnilnd
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libkgnilnd.*

# ovis-ldms-sampler-generic
%package sampler-generic
Summary: Generic LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-generic
%{summary}
%files sampler-generic
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libgeneric_sampler.*

# ovis-ldms-sampler-slurm
%package sampler-slurm
Summary: Slurm LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-slurm
%{summary}
%files sampler-slurm
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libslurm_sampler.*
%{_libdir}/ovis-ldms/libslurm_notifier.*
%if 0%{?_with_sos:1}
%{_libdir}/ovis-ldms/libstore_slurm.*
%endif

%package lib-streams
Summary: LDMS Stream Libraries
Group: Applications/System
Version: %{version}
%description lib-streams
%{summary}
%files lib-streams
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libblob_stream_writer.*
%{_libdir}/ovis-ldms/libstream_csv_store.*

# ovis-ldms-sampler-lustre2
%package sampler-lustre2
Summary: Lustre2 LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-lustre2
%{summary}
%files sampler-lustre2
%defattr(-,root,root)
%{_libdir}/ovis-ldms/liblustre2_*
%{_libdir}/ovis-ldms/liblustre_*

# ovis-ldms-sampler-meminfo
%package sampler-meminfo
Summary: Meminfo LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-meminfo
%{summary}
%files sampler-meminfo
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libmeminfo.*

# ovis-ldms-sampler-cgroup
%package sampler-cgroup
Summary: cgroup LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-cgroup
%{summary}
%files sampler-cgroup
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libsampler_cgroup.*

# ovis-ldms-sampler-papi
%package sampler-papi
Summary: PAPI LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-papi
%{summary}
%files sampler-papi
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libpapi_sampler.*
%{_libdir}/ovis-ldms/libpapi_hook.*
%if 0%{?_with_sos:1}
%{_libdir}/ovis-ldms/libstore_papi.*
%endif

# ovis-ldms-sampler-procdiskstats
%package sampler-procdiskstats
Summary: Procdiskstats LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-procdiskstats
%{summary}
%files sampler-procdiskstats
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libprocdiskstats.*

# ovis-ldms-sampler-procinterrupts
%package sampler-procinterrupts
Summary: procinterrupts LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-procinterrupts
%{summary}
%files sampler-procinterrupts
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libprocinterrupts.*

# ovis-ldms-sampler-procnetdev
%package sampler-procnetdev
Summary: procnetdev LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-procnetdev
%{summary}
%files sampler-procnetdev
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libprocnetdev.*
%{_libdir}/ovis-ldms/libprocnet.*

# ovis-ldms-sampler-procnfs
%package sampler-procnfs
Summary: procnfs LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-procnfs
%{summary}
%files sampler-procnfs
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libprocnfs.*

# ovis-ldms-sampler-procstat
%package sampler-procstat
Summary: procstat LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-procstat
%{summary}
%files sampler-procstat
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libprocstat.*

# ovis-ldms-sampler-synthetic
%package sampler-synthetic
Summary: synthetic LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-synthetic
%{summary}
%files sampler-synthetic
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libsynthetic.*

# ovis-ldms-sampler-sysclassib
%package sampler-sysclassib
Summary: sysclassib LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-sysclassib
%{summary}
%files sampler-sysclassib
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libsysclassib.*

# ovis-ldms-sampler-vmstat
%package sampler-vmstat
Summary: vmstat LDMSD Vmstat Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-vmstat
%{summary}
%files sampler-vmstat
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libvmstat.*

# ovis-ldms-sampler-all
%package sampler-all
Summary: all LDMSD All Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-all
%{summary}
%files sampler-all
%defattr(-,root,root)
%{_libdir}/ovis-ldms/liball_example.*

# ovis-ldms-sampler-edac
%package sampler-edac
Summary: edac LDMSD EDAC Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-edac
%{summary}
%files sampler-edac
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libedac.*

# ovis-ldms-sampler-lnet_stats
%package sampler-lnet_stats
Summary: Lustre Network Statistics LDMSD Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-lnet_stats
%{summary}
%files sampler-lnet_stats
%defattr(-,root,root)
%{_libdir}/ovis-ldms/liblnet_stats.*

# ovis-ldms-sampler-dstat
%package sampler-dstat
Summary: dstat LDMSD EDAC Sampler Plugin
Group: Applications/System
Version: %{version}
%description sampler-dstat
%{summary}
%files sampler-dstat
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libdstat.*

# ovis-ldms-sampler-syspapi
%package sampler-syspapi
Summary: syspapi
Group: Applications/System
Version: %{version}
%description sampler-syspapi
%{summary}
%files sampler-syspapi
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libsyspapi_sampler.*

# ovis-ldms-sampler-loadavg
%package sampler-loadavg
Summary: LDMSD Load average plugin
Group: Applications/System
Version: %{version}
%description sampler-loadavg
%{summary}
%files sampler-loadavg
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libloadavg.*

# ovis-ldms-sampler-hweventpapi
%package sampler-hweventpapi
Summary: LDMSD PAPI hardware event plugin
Group: Applications/System
Version: %{version}
%description sampler-hweventpapi
%{summary}
%files sampler-hweventpapi
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libhweventpapi.*

# ovis-ldms-sampler-rapl
%package sampler-rapl
Summary: LDMSD rapl plugin
Group: Applications/System
Version: %{version}
%description sampler-rapl
%{summary}
%files sampler-rapl
%defattr(-,root,root)
%{_libdir}/ovis-ldms/librapl.*

# ovis-ldms-sampler-hello
%package sampler-hello
Summary: LDMSD example sampler
Group: Applications/System
Version: %{version}
%description sampler-hello
%{summary}
%files sampler-hello
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libhello_sampler.*

# ovis-ldms-sampler-dcgm
%package sampler-dcgm
Summary: LDMSD example sampler
Group: Applications/System
Version: %{version}
%description sampler-dcgm
%{summary}
%files sampler-dcgm
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libdcgm.*
%{_libdir}/ovis-ldms/libdcgm_sampler.*

#################
# store plugins #
#################

# ovis-ldms-store-csv-common
%package store-csv-common
Summary: CSV LDMSD Store Plugin Common Library
Group: Applications/System
Version: %{version}
%description store-csv-common
%{summary}
%files store-csv-common
%defattr(-,root,root)
%{_libdir}/libldms_store_csv_common.*

# ovis-ldms-store-csv
%package store-csv
Summary: CSV LDMSD Store Plugin
Group: Applications/System
Requires: ovis-ldms-store-csv-common >= %{version}
Version: %{version}
%description store-csv
%{summary}
%files store-csv
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libstore_csv.*
%{_bindir}/ldms-csv-anonymize
%{_bindir}/ldms-csv-export-sos

# ovis-ldms-store-none
%package store-none
Summary: Null LDMSD Store Plugin
Group: Applications/System
Version: %{version}
%description store-none
%{summary}
%files store-none
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libstore_none.*

# ovis-ldms-store-function-csv
%package store-function-csv
Summary: LDMSD Function CSV Store Plugin
Group: Applications/System
Version: %{version}
Requires: ovis-ldms-store-csv-common >= %{version}
%description store-function-csv
%{summary}
%files store-function-csv
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libstore_function_csv.*

%if 0%{?_with_sos:1}
# ovis-ldms-store-sos
%package store-sos
Summary: LDMSD SOS Store Plugin
Group: Applications/System
Requires: sosdb >= 4.2.1
Version: %{version}
%description store-sos
%{summary}
%files store-sos
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libstore_sos.*
%endif

# ovis-ldms-store-rabbitv3
%package store-rabbitv3
Summary: RabbitMQ LDMSD Store Plugin
Group: Applications/System
Version: %{version}
%description store-rabbitv3
%{summary}
%files store-rabbitv3
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libstore_rabbitv3.*

# ovis-ldms-store-amqp
%package store-amqp
Summary: RabbitMQ LDMSD Store Plugin
Group: Applications/System
Version: %{version}
%description store-amqp
%{summary}
%files store-amqp
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libstore_amqp.*

# ovis-ldms-store-influx
%package store-influx
Summary: InfluxDB LDMSD Store Plugin
Group: Applications/System
Version: %{version}
%description store-influx
%{summary}
%files store-influx
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libstore_influx.*

# ovis-ldms-store-victoriametrics
%package store-victoriametrics
Summary: VictoriaMetrics LDMSD Store Plugin
Group: Applications/System
Version: %{version}
%description store-victoriametrics
%{summary}
%files store-victoriametrics
%defattr(-,root,root)
%{_libdir}/ovis-ldms/libstore_victoriametrics.*
%{_libdir}/ovis-ldms/libstore_jsondump.*
%{_libdir}/ovis-ldms/libsampler_jsondump.*

%package misc
Summary: Miscellaneous file in ovis-lib project.
Group: Development/Libraries
%description misc
Miscellaneous file in ovis-lib project.
%files misc
%{_libdir}/ovis-lib-configvars.sh
%{_sysconfdir}/

%posttrans misc
/bin/ln -fs %{_sysconfdir}/profile.d/set-ovis-variables.sh /etc/profile.d/set-ovis-variables.sh
/bin/ln -fs %{_sysconfdir}/ld.so.conf.d/ovis-ld-so.conf /etc/ld.so.conf.d/ovis-ld-so.conf
/sbin/ldconfig
%postun misc
/bin/rm -f /etc/profile.d/set-ovis-variables.sh
/bin/rm -f /etc/ld.so.conf.d/ovis-ld-so.conf
/sbin/ldconfig

%changelog

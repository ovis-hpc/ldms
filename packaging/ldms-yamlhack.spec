# Set topdir to be builddir/rpm
# note this is intentionally ignored by rpmbuild. must use
# commandline syntax in makefile.am to get this effect.
#% define _topdir %(echo $PWD)/toss
# do not set unfascist build
#%-define _unpackaged_files_terminate_build 0
#%-define _missing_doc_files_terminate_build 0

%define ldms_yamlhack System Environment/Libraries
# % global __strip /bin/true
%global _enable_debug_package 0
%global debug_package %{nil}
%global __os_install_post /usr/lib/rpm/brp-compress %{nil}
%if 0%{?rhel} && 0%{?rhel} <= 6
%{!?__python2: %global __python2 /opt/python-2.7/bin/python}
%{!?python2_sitelib: %global python2_sitelib %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")}
%{!?python2_sitearch: %global python2_sitearch %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib(1))")}
%endif

# Main package
Summary: OVIS LDMS workaround for missing libyaml-devel repo access
Name: ldms-yamlhack
Version: 0.1.4
Release: 11
License: BSD
Group: %{ldms_all}
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires: rpm >= 4.8.0 libyaml
BuildRequires: libyaml libyaml-devel
Conflicts: libyaml-devel
Url: http://ovis.ca.sandia.gov/

Prefix: /usr


%description
This package provides the LDMS needed libyaml.so alias for libyaml
normally available in libyaml-devel.

%prep

%build
#echo bTMPPATH %{_tmppath}
#rm -rf $RPM_BUILD_ROOT
#echo bBUILDROOT $RPM_BUILD_ROOT
#mkdir -p $RPM_BUILD_ROOT/%{_libdir}
#cp -a %{_libdir}/libyaml.so $RPM_BUILD_ROOT/%{_libdir}
#tree $RPM_BUILD_ROOT

%install
echo "do little"
mkdir -p %{buildroot}/%{_libdir}
install -p -m 755 %{_libdir}/libyaml.so %{buildroot}/%{_libdir}
tree %{buildroot}

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{_libdir}/*
#end core

%post 

%changelog
* Wed Sep 28 2016 Ben Allan <baallan@sandia.gov> 0.1.4-11
Workaround for rpm repo provisioning difficulty.

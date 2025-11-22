# add --with java_compile option, i.e. disable java_compile by default
%bcond_with java_compile

%global build_type %{?_build_type:%{_build_type}}
# 如果没有提供，则设置默认值
%if "%{build_type}" == ""
    %global build_type release
%endif

%global with_hcom_perf %{?_with_hcom_perf:%{_with_hcom_perf}}
# 如果没有提供，则设置默认值
%if "%{with_hcom_perf}" == ""
    %global with_hcom_perf 0
%endif

%global with_htracer_cli %{?_with_htracer_cli:%{_with_htracer_cli}}
# 如果没有提供，则设置默认值
%if "%{with_htracer_cli}" == ""
    %global with_htracer_cli 0
%endif

%if %{undefined rpm_version}
    %define rpm_version 1.0.0
%endif

%if %{undefined rpm_release}
    %define rpm_release B018
%endif

%if %{undefined rpm_build_date}
    %define rpm_build_date %(date +"%%Y-%%m-%%d-%%H:%%M:%%S")
%endif

%if %{undefined package_name}
    %define package_name ubs-comm
%endif

%global package_suffix ubs-comm

Name:           %{package_suffix}
Version       : %{rpm_version}
Release       : %{rpm_release}
Summary:        HCOM
License       : Proprietary
Provides      : Huawei Technologies Co., Ltd
Source0       : %{package_name}.tar.gz
BuildRoot     : %{_buildirootdir}/%{name}_%{version}-build
buildArch     : aarch64 x86_64

BuildRequires: make gcc cmake libboundscheck
Requires: libboundscheck

%description
HCOM是一个适用于C/S架构应用程序的高性能通信库

%package devel
Summary: Development header files and dynamic library for HCOM
Requires:       ubs-comm-lib = %{version}

%description devel
This package contains development header files and dynamic library for HCOM

%package lib
Summary: Dynamic library for HCOM

%description lib
This package contains dynamic library for HCOM

%prep
%setup -c -n %{name}_%{version}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/lib64/
mkdir -p  %{buildroot}/usr/local/jars/hcom
mkdir -p %{buildroot}/usr/include/hcom/capi
mkdir -p %{buildroot}/usr/local/bin

cp %{_builddir}/%{name}_%{version}/%{package_name}/hcom/lib/*  %{buildroot}/usr/lib64/
cp -r %{_builddir}/%{name}_%{version}/%{package_name}/hcom/include/hcom/*  %{buildroot}/usr/include/hcom/

%if %{with java_compile}
    cp %{_builddir}/%{name}_%{version}/%{package_name}/hcom/jars/*  %{buildroot}/usr/local/jars/hcom/
%endif

%if %{with_hcom_perf}
    cp -r %{_builddir}/%{name}_%{version}/%{package_name}/hcom/hcom_perf  %{buildroot}/usr/local/bin/
%endif

%if %{with_htracer_cli}
    cp -r %{_builddir}/%{name}_%{version}/%{package_name}/hcom/bin/htracer_cli  %{buildroot}/usr/local/bin/
%endif

%files devel
%defattr(-,root,root)
%{_prefix}/include/hcom/capi/*.h
%{_prefix}/include/hcom/*.h
%if %{with_hcom_perf} || %{with_htracer_cli}
    %{_prefix}/local/bin/*
%endif
%{_prefix}/lib64/libhcom.so
%{_prefix}/lib64/libhcom_static.a
%if %{with java_compile}
    %{_prefix}/local/jars/hcom/*.jar
%endif

%files lib
%defattr(-,root,root)
%{_prefix}/lib64/libhcom.so.0
%{_prefix}/lib64/libhcom.so.0.0.1

%define __os_install_post %{nil}
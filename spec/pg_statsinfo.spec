# SPEC file for pg_statsinfo
# Copyright (c) 2009-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION

# Original declaration for pg_statsinfo rpmbuild #

%define _pgdir   /usr/pgsql-14
%define _bindir  %{_pgdir}/bin
%define _libdir  %{_pgdir}/lib
%define _datadir %{_pgdir}/share

## Set general information for pg_statsinfo.
Name:       pg_statsinfo
Version:    14.2
Release:    1%{?dist}
Summary:    Performance monitoring tool for PostgreSQL
Group:      Applications/Databases
License:    BSD
URL:        https://github.com/ossc-db/pg_statsinfo
Source0:    %{name}-%{version}.tar.gz
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-%(%{__id_u} -n)

## We use postgresql-devel package
BuildRequires:  postgresql14-devel
%if %{rhel} == 7
BuildRequires:  llvm-toolset-7 llvm5.0
%endif
%if %{rhel} == 8
BuildRequires:  llvm >= 6
%endif
%if %{rhel} == 9
BuildRequires:  llvm >= 15
%endif

%description
pg_statsinfo monitors an instance of PostgreSQL server and gather
the statistics and activities of the server as snapshots.

%package llvmjit
Requires: postgresql14-llvmjit
Requires: pg_statsinfo = %{version}
Summary:  Just-in-time compilation support for pg_statsinfo

%description llvmjit
Just-in-time compilation support for pg_statsinfo

## pre work for build pg_statsinfo
%prep
%setup -q -n %{name}-%{version}

## Set variables for build environment
%build
USE_PGXS=1 make %{?_smp_mflags}

## Set variables for install
%install
rm -rf %{buildroot}
USE_PGXS=1 make DESTDIR=%{buildroot} install

%clean
rm -rf %{buildroot}

## Set files for this packages
%files
%defattr(-,root,root)
%{_bindir}/pg_statsinfo
%{_bindir}/pg_statsinfod
%{_bindir}/archive_pglog.sh
%{_libdir}/pg_statsinfo.so
%{_datadir}/contrib/pg_statsrepo.sql
%{_datadir}/contrib/pg_statsrepo_alert.sql
%{_datadir}/contrib/uninstall_pg_statsrepo.sql
%{_datadir}/contrib/pg_statsinfo.sql
%{_datadir}/contrib/uninstall_pg_statsinfo.sql

%files llvmjit
%defattr(-,root,root)
%{_libdir}/bitcode/pg_statsinfo.index.bc
%{_libdir}/bitcode/pg_statsinfo/libstatsinfo.bc
%{_libdir}/bitcode/pg_statsinfo/last_xact_activity.bc
%{_libdir}/bitcode/pg_statsinfo/pg_control.bc
%{_libdir}/bitcode/pg_statsinfo/port.bc
%{_libdir}/bitcode/pg_statsinfo/wait_sampling.bc
%{_libdir}/bitcode/pg_statsinfo/pgut/pgut-spi.bc

## Script to run just before installing the package
%pre
# Check if we can safely upgrade.
# An upgrade is only safe if it's from one of our RPMs in the same version family.
installed=$(rpm -q --whatprovides pg_statsinfo 2> /dev/null)
if [ ${?} -eq 0 -a -n "${installed}" ] ; then
	old_version=$(rpm -q --queryformat='%{VERSION}' "${installed}" 2>&1)
	new_version='%{version}'

	new_family=$(echo ${new_version} | cut -d '.' -f 1)
	old_family=$(echo ${old_version} | cut -d '.' -f 1)

	[ -z "${old_family}" ] && old_family="<unrecognized version ${old_version}>"
	[ -z "${new_family}" ] && new_family="<bad package specification: version ${new_version}>"

	if [ "${old_family}" != "${new_family}" ] ; then
		cat << EOF >&2
******************************************************************
A pg_statsinfo package ($installed) is already installed.
Could not upgrade pg_statsinfo "${old_version}" to "${new_version}".
A manual upgrade is required.

 - Remove 'pg_statsinfo' from shared_preload_libraries and
   all of pg_statsinfo.* parameters in postgresql.conf.
 - Restart the monitored database.
 - Uninstall statsinfo schema from monitored database.
 - Uninstall statsrepo schema from repository database.
   Snapshot is removed by dropping the statsrepo schema.
   Therefore, please backup the repository database as necessary.
 - Remove the existing pg_statsinfo package.
 - Install the new pg_statsinfo package.
 - Restore the parameters of postgresql.conf which was removed first.
 - Restart the monitored database.

This is a brief description of the upgrade process.
Important details can be found in the pg_statsinfo manual.
******************************************************************
EOF
		exit 1
	fi
fi

# History of pg_statsinfo-v14 RPM.
%changelog
* Fri Jul  7 2023 - NTT OSS Center 14.2-1
- pg_statsinfo 14.2 released
* Tue Nov  1 2022 - NTT OSS Center 14.1-1
- pg_statsinfo 14.1 released
* Tue Feb  1 2022 - NTT OSS Center 14.0-1
- pg_statsinfo 14.0 released
* Mon Dec  14 2020 - NTT OSS Center 13.0-1
- pg_statsinfo 13.0 released
* Fri Feb  28 2020 - NTT OSS Center 12.1-1
- pg_statsinfo 12.1 released
* Fri Jan  24 2020 - NTT OSS Center 12.0-1
- pg_statsinfo 12.0 released

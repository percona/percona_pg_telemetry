%global sname percona-pg-telemetry
%global pgrel @@PG_REL@@
%global rpm_release @@RPM_RELEASE@@
%global pginstdir /usr/pgsql-@@PG_REL@@/

Summary:        Statistics collector for PostgreSQL
Name:           %{sname}%{pgrel}
Version:        @@VERSION@@
Release:        %{rpm_release}%{?dist}
License:        PostgreSQL
Source0:        percona-pg-telemetry%{pgrel}-%{version}.tar.gz
URL:            https://github.com/percona/percona_pg_telemetry
BuildRequires:  percona-postgresql%{pgrel}-devel
Provides:       percona-pg-telemetry%{pgrel}
Conflicts:      percona-pg-telemetry%{pgrel}
Obsoletes:      percona-pg-telemetry%{pgrel} <= %{version}-%{release}
Epoch:          1
Packager:       Percona Development Team <https://jira.percona.com>
Vendor:         Percona, Inc

%description
The percona_pg_telemetry is an extension for Percona telemetry data collection for PostgreSQL.

%prep
%setup -q -n percona-pg-telemetry%{pgrel}-%{version}


%build
sed -i 's:PG_CONFIG = pg_config:PG_CONFIG = /usr/pgsql-%{pgrel}/bin/pg_config:' Makefile
%{__make} USE_PGXS=1 %{?_smp_mflags}


%install
%{__rm} -rf %{buildroot}
%{__make} USE_PGXS=1 %{?_smp_mflags} install DESTDIR=%{buildroot}
%{__install} -d %{buildroot}%{pginstdir}/share/extension
%{__install} -m 755 README.md %{buildroot}%{pginstdir}/share/extension/README-percona_pg_telemetry


%clean
%{__rm} -rf %{buildroot}

%pre -n percona-pg-telemetry%{pgrel}
if [ $1 == 1 ]; then
  if ! getent passwd postgres > /dev/null 2>&1; then
    groupadd -g 26 -o -r postgres >/dev/null 2>&1 || :
    /usr/sbin/useradd -M -g postgres -o -r -d /var/lib/pgsql -s /bin/bash \
        -c "PostgreSQL Server" -u 26 postgres >/dev/null 2>&1 || :
  fi
fi

%post -n percona-pg-telemetry%{pgrel}
# Transitional cleanup: percona-pg-telemetry <= 1.1 managed this dir for the
# telemetry-agent. The extension no longer uses it. Remove in a future release.
if [ -d /usr/local/percona/telemetry/pg ] && [ -z "$(ls -A /usr/local/percona/telemetry/pg 2>/dev/null)" ]; then
    rmdir /usr/local/percona/telemetry/pg 2>/dev/null || :
fi
exit 0

%files
%defattr(755,root,root,755)
%doc %{pginstdir}/share/extension/README-percona_pg_telemetry
%{pginstdir}/lib/percona_pg_telemetry.so
%{pginstdir}/share/extension/percona_pg_telemetry--*.sql
%{pginstdir}/share/extension/percona_pg_telemetry.control
%{pginstdir}/lib/bitcode/percona_pg_telemetry*.bc
%{pginstdir}/lib/bitcode/percona_pg_telemetry/*.bc


%changelog
* Tue Jul 21 2026 Surabhi Bhat <surabhi.bhat@percona.com> - 1.2.0-3
- Drop hard dependency on percona-telemetry-agent. The extension has been a
  no-op stub since 1.2.0 and no longer writes to /usr/local/percona/telemetry/pg,
  so the agent, the percona-telemetry group, and the drop directory are no
  longer required.
- Drop %post/%postun management of /usr/local/percona/telemetry/pg and of
  postgres group membership in percona-telemetry.
* Fri Apr 26 2024 Surabhi Bhat <surabhi.bhat@percona.com> - 1.0.0-1
- Initial build

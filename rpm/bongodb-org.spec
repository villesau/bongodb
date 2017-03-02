Name: bongodb-org
Prefix: /usr
Conflicts: bongo-10gen-enterprise, bongo-10gen-enterprise-server, bongo-10gen-unstable, bongo-10gen-unstable-enterprise, bongo-10gen-unstable-enterprise-bongos, bongo-10gen-unstable-enterprise-server, bongo-10gen-unstable-enterprise-shell, bongo-10gen-unstable-enterprise-tools, bongo-10gen-unstable-bongos, bongo-10gen-unstable-server, bongo-10gen-unstable-shell, bongo-10gen-unstable-tools, bongo18-10gen, bongo18-10gen-server, bongo20-10gen, bongo20-10gen-server, bongodb, bongodb-server, bongodb-dev, bongodb-clients, bongodb-10gen, bongodb-10gen-enterprise, bongodb-10gen-unstable, bongodb-10gen-unstable-enterprise, bongodb-10gen-unstable-enterprise-bongos, bongodb-10gen-unstable-enterprise-server, bongodb-10gen-unstable-enterprise-shell, bongodb-10gen-unstable-enterprise-tools, bongodb-10gen-unstable-bongos, bongodb-10gen-unstable-server, bongodb-10gen-unstable-shell, bongodb-10gen-unstable-tools, bongodb-enterprise, bongodb-enterprise-bongos, bongodb-enterprise-server, bongodb-enterprise-shell, bongodb-enterprise-tools, bongodb-nightly, bongodb-org-unstable, bongodb-org-unstable-bongos, bongodb-org-unstable-server, bongodb-org-unstable-shell, bongodb-org-unstable-tools, bongodb-stable, bongodb18-10gen, bongodb20-10gen, bongodb-enterprise-unstable, bongodb-enterprise-unstable-bongos, bongodb-enterprise-unstable-server, bongodb-enterprise-unstable-shell, bongodb-enterprise-unstable-tools
Version: %{dynamic_version}
Release: %{dynamic_release}%{?dist}
Obsoletes: bongo-10gen
Provides: bongo-10gen
Summary: BongoDB open source document-oriented database system (metapackage)
License: AGPL 3.0
URL: http://www.bongodb.org
Group: Applications/Databases
Requires: bongodb-org-server = %{version}, bongodb-org-shell = %{version}, bongodb-org-bongos = %{version}, bongodb-org-tools = %{version}

Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
BongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, BongoDB provides high performance for both reads and writes. BongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

BongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

BongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

BongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This metapackage will install the bongo shell, import/export tools, other client utilities, server software, default configuration, and systemd service files.

%package server
Summary: BongoDB database server
Group: Applications/Databases
Requires: openssl
Conflicts: bongo-10gen-enterprise, bongo-10gen-enterprise-server, bongo-10gen-unstable, bongo-10gen-unstable-enterprise, bongo-10gen-unstable-enterprise-bongos, bongo-10gen-unstable-enterprise-server, bongo-10gen-unstable-enterprise-shell, bongo-10gen-unstable-enterprise-tools, bongo-10gen-unstable-bongos, bongo-10gen-unstable-server, bongo-10gen-unstable-shell, bongo-10gen-unstable-tools, bongo18-10gen, bongo18-10gen-server, bongo20-10gen, bongo20-10gen-server, bongodb, bongodb-server, bongodb-dev, bongodb-clients, bongodb-10gen, bongodb-10gen-enterprise, bongodb-10gen-unstable, bongodb-10gen-unstable-enterprise, bongodb-10gen-unstable-enterprise-bongos, bongodb-10gen-unstable-enterprise-server, bongodb-10gen-unstable-enterprise-shell, bongodb-10gen-unstable-enterprise-tools, bongodb-10gen-unstable-bongos, bongodb-10gen-unstable-server, bongodb-10gen-unstable-shell, bongodb-10gen-unstable-tools, bongodb-enterprise, bongodb-enterprise-bongos, bongodb-enterprise-server, bongodb-enterprise-shell, bongodb-enterprise-tools, bongodb-nightly, bongodb-org-unstable, bongodb-org-unstable-bongos, bongodb-org-unstable-server, bongodb-org-unstable-shell, bongodb-org-unstable-tools, bongodb-stable, bongodb18-10gen, bongodb20-10gen, bongodb-enterprise-unstable, bongodb-enterprise-unstable-bongos, bongodb-enterprise-unstable-server, bongodb-enterprise-unstable-shell, bongodb-enterprise-unstable-tools
Obsoletes: bongo-10gen-server
Provides: bongo-10gen-server

%description server
BongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, BongoDB provides high performance for both reads and writes. BongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

BongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

BongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

BongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains the BongoDB server software, default configuration files, and systemd service files.

%package shell
Summary: BongoDB shell client
Group: Applications/Databases
Requires: openssl
Conflicts: bongo-10gen-enterprise, bongo-10gen-enterprise-server, bongo-10gen-unstable, bongo-10gen-unstable-enterprise, bongo-10gen-unstable-enterprise-bongos, bongo-10gen-unstable-enterprise-server, bongo-10gen-unstable-enterprise-shell, bongo-10gen-unstable-enterprise-tools, bongo-10gen-unstable-bongos, bongo-10gen-unstable-server, bongo-10gen-unstable-shell, bongo-10gen-unstable-tools, bongo18-10gen, bongo18-10gen-server, bongo20-10gen, bongo20-10gen-server, bongodb, bongodb-server, bongodb-dev, bongodb-clients, bongodb-10gen, bongodb-10gen-enterprise, bongodb-10gen-unstable, bongodb-10gen-unstable-enterprise, bongodb-10gen-unstable-enterprise-bongos, bongodb-10gen-unstable-enterprise-server, bongodb-10gen-unstable-enterprise-shell, bongodb-10gen-unstable-enterprise-tools, bongodb-10gen-unstable-bongos, bongodb-10gen-unstable-server, bongodb-10gen-unstable-shell, bongodb-10gen-unstable-tools, bongodb-enterprise, bongodb-enterprise-bongos, bongodb-enterprise-server, bongodb-enterprise-shell, bongodb-enterprise-tools, bongodb-nightly, bongodb-org-unstable, bongodb-org-unstable-bongos, bongodb-org-unstable-server, bongodb-org-unstable-shell, bongodb-org-unstable-tools, bongodb-stable, bongodb18-10gen, bongodb20-10gen, bongodb-enterprise-unstable, bongodb-enterprise-unstable-bongos, bongodb-enterprise-unstable-server, bongodb-enterprise-unstable-shell, bongodb-enterprise-unstable-tools
Obsoletes: bongo-10gen-shell
Provides: bongo-10gen-shell

%description shell
BongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, BongoDB provides high performance for both reads and writes. BongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

BongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

BongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

BongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains the bongo shell.

%package bongos
Summary: BongoDB sharded cluster query router
Group: Applications/Databases
Conflicts: bongo-10gen-enterprise, bongo-10gen-enterprise-server, bongo-10gen-unstable, bongo-10gen-unstable-enterprise, bongo-10gen-unstable-enterprise-bongos, bongo-10gen-unstable-enterprise-server, bongo-10gen-unstable-enterprise-shell, bongo-10gen-unstable-enterprise-tools, bongo-10gen-unstable-bongos, bongo-10gen-unstable-server, bongo-10gen-unstable-shell, bongo-10gen-unstable-tools, bongo18-10gen, bongo18-10gen-server, bongo20-10gen, bongo20-10gen-server, bongodb, bongodb-server, bongodb-dev, bongodb-clients, bongodb-10gen, bongodb-10gen-enterprise, bongodb-10gen-unstable, bongodb-10gen-unstable-enterprise, bongodb-10gen-unstable-enterprise-bongos, bongodb-10gen-unstable-enterprise-server, bongodb-10gen-unstable-enterprise-shell, bongodb-10gen-unstable-enterprise-tools, bongodb-10gen-unstable-bongos, bongodb-10gen-unstable-server, bongodb-10gen-unstable-shell, bongodb-10gen-unstable-tools, bongodb-enterprise, bongodb-enterprise-bongos, bongodb-enterprise-server, bongodb-enterprise-shell, bongodb-enterprise-tools, bongodb-nightly, bongodb-org-unstable, bongodb-org-unstable-bongos, bongodb-org-unstable-server, bongodb-org-unstable-shell, bongodb-org-unstable-tools, bongodb-stable, bongodb18-10gen, bongodb20-10gen, bongodb-enterprise-unstable, bongodb-enterprise-unstable-bongos, bongodb-enterprise-unstable-server, bongodb-enterprise-unstable-shell, bongodb-enterprise-unstable-tools
Obsoletes: bongo-10gen-bongos
Provides: bongo-10gen-bongos

%description bongos
BongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, BongoDB provides high performance for both reads and writes. BongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

BongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

BongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

BongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains bongos, the BongoDB sharded cluster query router.

%package tools
Summary: BongoDB tools
Group: Applications/Databases
Requires: openssl
Conflicts: bongo-10gen-enterprise, bongo-10gen-enterprise-server, bongo-10gen-unstable, bongo-10gen-unstable-enterprise, bongo-10gen-unstable-enterprise-bongos, bongo-10gen-unstable-enterprise-server, bongo-10gen-unstable-enterprise-shell, bongo-10gen-unstable-enterprise-tools, bongo-10gen-unstable-bongos, bongo-10gen-unstable-server, bongo-10gen-unstable-shell, bongo-10gen-unstable-tools, bongo18-10gen, bongo18-10gen-server, bongo20-10gen, bongo20-10gen-server, bongodb, bongodb-server, bongodb-dev, bongodb-clients, bongodb-10gen, bongodb-10gen-enterprise, bongodb-10gen-unstable, bongodb-10gen-unstable-enterprise, bongodb-10gen-unstable-enterprise-bongos, bongodb-10gen-unstable-enterprise-server, bongodb-10gen-unstable-enterprise-shell, bongodb-10gen-unstable-enterprise-tools, bongodb-10gen-unstable-bongos, bongodb-10gen-unstable-server, bongodb-10gen-unstable-shell, bongodb-10gen-unstable-tools, bongodb-enterprise, bongodb-enterprise-bongos, bongodb-enterprise-server, bongodb-enterprise-shell, bongodb-enterprise-tools, bongodb-nightly, bongodb-org-unstable, bongodb-org-unstable-bongos, bongodb-org-unstable-server, bongodb-org-unstable-shell, bongodb-org-unstable-tools, bongodb-stable, bongodb18-10gen, bongodb20-10gen, bongodb-enterprise-unstable, bongodb-enterprise-unstable-bongos, bongodb-enterprise-unstable-server, bongodb-enterprise-unstable-shell, bongodb-enterprise-unstable-tools
Obsoletes: bongo-10gen-tools
Provides: bongo-10gen-tools

%description tools
BongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, BongoDB provides high performance for both reads and writes. BongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

BongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

BongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

BongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package contains standard utilities for interacting with BongoDB.

%package devel
Summary: Headers and libraries for BongoDB development
Group: Applications/Databases
Conflicts: bongo-10gen-enterprise, bongo-10gen-enterprise-server, bongo-10gen-unstable, bongo-10gen-unstable-enterprise, bongo-10gen-unstable-enterprise-bongos, bongo-10gen-unstable-enterprise-server, bongo-10gen-unstable-enterprise-shell, bongo-10gen-unstable-enterprise-tools, bongo-10gen-unstable-bongos, bongo-10gen-unstable-server, bongo-10gen-unstable-shell, bongo-10gen-unstable-tools, bongo18-10gen, bongo18-10gen-server, bongo20-10gen, bongo20-10gen-server, bongodb, bongodb-server, bongodb-dev, bongodb-clients, bongodb-10gen, bongodb-10gen-enterprise, bongodb-10gen-unstable, bongodb-10gen-unstable-enterprise, bongodb-10gen-unstable-enterprise-bongos, bongodb-10gen-unstable-enterprise-server, bongodb-10gen-unstable-enterprise-shell, bongodb-10gen-unstable-enterprise-tools, bongodb-10gen-unstable-bongos, bongodb-10gen-unstable-server, bongodb-10gen-unstable-shell, bongodb-10gen-unstable-tools, bongodb-enterprise, bongodb-enterprise-bongos, bongodb-enterprise-server, bongodb-enterprise-shell, bongodb-enterprise-tools, bongodb-nightly, bongodb-org-unstable, bongodb-org-unstable-bongos, bongodb-org-unstable-server, bongodb-org-unstable-shell, bongodb-org-unstable-tools, bongodb-stable, bongodb18-10gen, bongodb20-10gen, bongodb-enterprise-unstable, bongodb-enterprise-unstable-bongos, bongodb-enterprise-unstable-server, bongodb-enterprise-unstable-shell, bongodb-enterprise-unstable-tools
Obsoletes: bongo-10gen-devel
Provides: bongo-10gen-devel

%description devel
BongoDB is built for scalability, performance and high availability, scaling from single server deployments to large, complex multi-site architectures. By leveraging in-memory computing, BongoDB provides high performance for both reads and writes. BongoDB’s native replication and automated failover enable enterprise-grade reliability and operational flexibility.

BongoDB is an open-source database used by companies of all sizes, across all industries and for a wide variety of applications. It is an agile database that allows schemas to change quickly as applications evolve, while still providing the functionality developers expect from traditional databases, such as secondary indexes, a full query language and strict consistency.

BongoDB has a rich client ecosystem including hadoop integration, officially supported drivers for 10 programming languages and environments, as well as 40 drivers supported by the user community.

BongoDB features:
* JSON Data Model with Dynamic Schemas
* Auto-Sharding for Horizontal Scalability
* Built-In Replication for High Availability
* Rich Secondary Indexes, including geospatial
* TTL indexes
* Text Search
* Aggregation Framework & Native MapReduce

This package provides the BongoDB static library and header files needed to develop BongoDB client software.

%prep
%setup

%build

%install
mkdir -p $RPM_BUILD_ROOT/usr
cp -rv bin $RPM_BUILD_ROOT/usr
mkdir -p $RPM_BUILD_ROOT/usr/share/man/man1
cp debian/*.1 $RPM_BUILD_ROOT/usr/share/man/man1/
# FIXME: remove this rm when bongosniff is back in the package
rm -v $RPM_BUILD_ROOT/usr/share/man/man1/bongosniff.1*
mkdir -p $RPM_BUILD_ROOT/etc
cp -v rpm/bongod.conf $RPM_BUILD_ROOT/etc/bongod.conf
mkdir -p $RPM_BUILD_ROOT/lib/systemd/system
cp -v rpm/bongod.service $RPM_BUILD_ROOT/lib/systemd/system
mkdir -p $RPM_BUILD_ROOT/var/lib/bongo
mkdir -p $RPM_BUILD_ROOT/var/log/bongodb
mkdir -p $RPM_BUILD_ROOT/var/run/bongodb
touch $RPM_BUILD_ROOT/var/log/bongodb/bongod.log

%clean
rm -rf $RPM_BUILD_ROOT

%pre server
if ! /usr/bin/id -g bongod &>/dev/null; then
    /usr/sbin/groupadd -r bongod
fi
if ! /usr/bin/id bongod &>/dev/null; then
    /usr/sbin/useradd -M -r -g bongod -d /var/lib/bongo -s /bin/false   -c bongod bongod > /dev/null 2>&1
fi

%post server
if test $1 = 1
then
  /usr/bin/systemctl enable bongod
fi

%preun server
if test $1 = 0
then
  /usr/bin/systemctl disable bongod
fi

%postun server
if test $1 -ge 1
then
  /usr/bin/systemctl restart bongod >/dev/null 2>&1 || :
fi

%files

%files server
%defattr(-,root,root,-)
%config(noreplace) /etc/bongod.conf
%{_bindir}/bongod
%{_mandir}/man1/bongod.1*
/lib/systemd/system/bongod.service
%attr(0755,bongod,bongod) %dir /var/lib/bongo
%attr(0755,bongod,bongod) %dir /var/log/bongodb
%attr(0755,bongod,bongod) %dir /var/run/bongodb
%attr(0640,bongod,bongod) %config(noreplace) %verify(not md5 size mtime) /var/log/bongodb/bongod.log
%doc GNU-AGPL-3.0
%doc README
%doc THIRD-PARTY-NOTICES
%doc MPL-2



%files shell
%defattr(-,root,root,-)
%{_bindir}/bongo
%{_mandir}/man1/bongo.1*

%files bongos
%defattr(-,root,root,-)
%{_bindir}/bongos
%{_mandir}/man1/bongos.1*

%files tools
%defattr(-,root,root,-)
#%doc README GNU-AGPL-3.0.txt

%{_bindir}/bsondump
%{_bindir}/bongodump
%{_bindir}/bongoexport
%{_bindir}/bongofiles
%{_bindir}/bongoimport
%{_bindir}/bongooplog
%{_bindir}/bongoperf
%{_bindir}/bongorestore
%{_bindir}/bongotop
%{_bindir}/bongostat

%{_mandir}/man1/bsondump.1*
%{_mandir}/man1/bongodump.1*
%{_mandir}/man1/bongoexport.1*
%{_mandir}/man1/bongofiles.1*
%{_mandir}/man1/bongoimport.1*
%{_mandir}/man1/bongooplog.1*
%{_mandir}/man1/bongoperf.1*
%{_mandir}/man1/bongorestore.1*
%{_mandir}/man1/bongotop.1*
%{_mandir}/man1/bongostat.1*

%changelog
* Mon Oct 10 2016 Sam Kleinman <sam@bongodb.com>
- Support for systemd init processes.

* Thu Dec 19 2013 Ernie Hershey <ernie.hershey@bongodb.com>
- Packaging file cleanup

* Thu Jan 28 2010 Richard M Kreuter <richard@10gen.com>
- Minor fixes.

* Sat Oct 24 2009 Joe Miklojcik <jmiklojcik@shopwiki.com> -
- Wrote bongo.spec.

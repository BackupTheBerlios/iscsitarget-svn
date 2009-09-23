##
## Global Package Definitions
##

## IET Release
%define iet_version 0.4.17

## Package Revision
%define revision 1

## Build Options
# Build DKMS kernel module
%define dkms 0

# Build from SVN repository
%define svn 0

## Package Definitions
#
%if %svn
%define svn_url http://svn.berlios.de/svnroot/repos/iscsitarget/trunk
%define iet_version %(svn info --non-interactive %{svn_url} | awk '{if ($1 == "Revision:") {print "svn_r"$2}}')
%endif

## Platform Definitions
# Define kernel version information
%{!?kernel:	%define kernel %(uname -r)}

%define kver	%(echo %{kernel} | sed -e 's/smp//' -e 's/bigmem//' -e 's/enterprise//')
%define ktype	%(echo kernel-%{kernel} | sed -e 's/%{kver}//' -e 's/-$//')
%define krel	%(echo %{kver} | sed -e 's/-/_/g')
%define kminor	%(echo %{kernel} | sed -e 's/.*\\([0-9][0-9]*\\)-.*/\\1/')


##
## Main Package
##

## Information
Summary: iSCSI Enterprise Target
Name: iscsitarget
Version: %{?iet_version: %{iet_version}}
Release: %{?revision: %{revision}}
License: GPL
Group: System Environment/Daemons
URL: http://sourceforge.net/projects/iscsitarget/
Packager: IET development team <iscsitarget-devel@lists.sourceforge.net>

## Source files
%if %svn
%else
Source0: %{name}-%{version}.tar.gz
%endif

## Patches

## Install Requirements
Requires: kmod-%{name} = %{version}

## Build Requirements
BuildRequires: kernel >= 2.6
BuildRequires: %{ktype}-devel = %{kver}, gcc, make, patch, binutils, /usr/bin/install, openssl-devel
%if %svn
BuildRequires: subversion
%endif

## Build Definitions
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

## Description
%description
iSCSI Enterprise Target


##
## Kernel Module
##
%if %dkms
%ifarch noarch
%package -n kmod-%{name}

## Information
Summary: iSCSI Enterprise Target kernel module
Group: System Environment/Kernel
Release: %{release}_dkms

## Install Requirements
Requires: dkms >= 2

## Install Provides
Provides: kmod-%{name}

## Description
%description -n kmod-%{name}
iSCSI Enterprise Target kernel module
%endif
%else
%package -n kmod-%{name}

## Information
Summary: iSCSI Enterprise Target kernel module
Group: System Environment/Kernel
Release: %{release}_%{krel}

## Install Requirements
Requires: %{ktype} = %{kver}

## Install Provides
Provides: kmod-%{name}

## Description
%description -n kmod-%{name}
iSCSI Enterprise Target kernel module
%endif


##
## Package Creation
##

## Preparation
%prep


## Setup
%if %svn
%setup -q -T -c -n %{name}-%{version}
svn export --force --non-interactive -q %{svn_url} .
sed -i -e "s/\(#define IET_VERSION_STRING\).*/\1\t\"%{version}\"/" include/iet_u.h

%else
%setup -q -n %{name}-%{version}
%endif


## Patch


## Build
%build
%ifnarch noarch
%if %dkms
%{__make} usr
%else
%{__make} KERNELSRC=/lib/modules/%{kernel}/build
%endif
%endif


## Installation
%install
%{__rm} -rf %{buildroot}
%ifnarch noarch
%if %dkms
%{__make} install-usr install-doc install-etc KERNELSRC=/lib/modules/%{kernel}/build DISTDIR=%{buildroot}
%else
%{__make} install-files KERNELSRC=/lib/modules/%{kernel}/build DISTDIR=%{buildroot}
%endif
mkdir -p %{buildroot}/etc/rc.d
mv %{buildroot}/etc/init.d %{buildroot}/etc/rc.d
rm -rf %{buildroot}/usr/share/doc/iscsitarget
%endif
%if %dkms
%ifarch noarch
%{__make} distclean
mkdir -p %{buildroot}/usr/src/%{name}-%{version}
cp -r COPYING include kernel patches %{buildroot}/usr/src/%{name}-%{version}
sed -e "s/PACKAGE_VERSION=.*/PACKAGE_VERSION=\"%{version}\"/" dkms.conf >%{buildroot}/usr/src/%{name}-%{version}/dkms.conf
%endif
%else
rm -f %{buildroot}/lib/modules/%{kernel}/modules.*
%endif


## Cleaning
%clean
%{__rm} -rf %{buildroot}


## Post-Install Script
%ifnarch noarch
%post
/sbin/chkconfig --add iscsi-target
%endif


## Pre-Uninstall Script
%ifnarch noarch
%preun
if [ "$1" = 0 ]; then
    /sbin/service iscsi-target stop &>/dev/null
    /sbin/chkconfig --del iscsi-target
fi
%endif


## Post-Uninstall Script
%ifnarch noarch
%postun
if [ "$1" != 0 ]; then
    /sbin/service iscsi-target condrestart 2>&1 > /dev/null
fi
%endif


## Post-Install Script (Kernel Module)
%if %dkms
%ifarch noarch
%post -n kmod-%{name}
/usr/sbin/dkms add -m %{name} -v %{version}
/usr/sbin/dkms build -m %{name} -v %{version}
/usr/sbin/dkms install -m %{name} -v %{version}
%endif
%else
%post -n kmod-%{name}
/sbin/depmod %{kernel} -A
%endif


## Pre-Uninstall Script (Kernel Module)
%if %dkms
%ifarch noarch
%preun -n kmod-%{name}
/usr/sbin/dkms remove -m %{name} -v %{version} --all
%endif
%else
%preun -n kmod-%{name}
modprobe -r -q --set-version %{kernel} iscsi_trgt
/sbin/depmod %{kernel} -A
%endif


## File Catalog
%ifnarch noarch
%files
%defattr(-, root, root)
%{_sbindir}/*
%{_mandir}/man?/*
%{_sysconfdir}/rc.d/init.d/*
%config(noreplace) %{_sysconfdir}/iet/ietd.conf
%config(noreplace) %{_sysconfdir}/iet/initiators.allow
%config(noreplace) %{_sysconfdir}/iet/targets.allow
%doc ChangeLog COPYING README README.vmware README.initiators
%endif


## File Catalog (Kernel Module)
%if %dkms
%ifarch noarch
%files -n kmod-%{name}
%defattr(-, root, root)
/usr/src/%{name}-%{version}/*
%endif
%else
%files -n kmod-%{name}
%defattr(-, root, root)
/lib/modules/%{kernel}/kernel/iscsi/iscsi_trgt.ko
%endif


%changelog
* Wed Sep 22 2009 Ross Walker <rswwalker at gmail dot com> - 0.4.17-226
- Updated file catalog for new config directory

* Wed Sep 09 2009 Ross Walker <rswwalker at gmail dot com> - 0.4.17-226
- Added ability to build directly from subversion repo
- Added ability to build dkms kernel module

* Mon Nov 10 2008 Ross Walker <rswwalker@gmail.com> - 0.4.17-177
- Changed kernel-module naming to kmod
- Updated versioning

* Fri Feb 16 2007 Ross Walker <rswwalker@gmail.com> - 0.4.14-96
- Reworked spec file for latest release
- Commented and cleaned up sections
- Added additional documents to %files

* Mon Nov 21 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl> - 0.4.13-0.1266.1
- upstream snapshot 1266
- added condrestart patch
- stop and start service on update or removal

* Sun Nov 13 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl> - 0.4.13-0.1264.2
- run %post and %preun for kernel package, not main package

* Sun Nov 13 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl> - 0.4.13-0.1264.1
- updated to snapshot 1264

* Thu Nov 03 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl> - 0.4.12-6
- added openssl-devel build requirement
- removed '.ko' extension in modprobe command

* Wed Nov 02 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl> - 0.4.12-5
- fixed kernel-devel BuildRequires

* Fri Sep 23 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl> - 0.4.12-4
- fixed modprobe -r 'FATAL' message
- run depmod with correct kernel version

* Fri Sep 23 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl> - 0.4.12-3
- added config files
- set kernel module file permissions to 744
- fixed provides/requires of kernel module
- removed BuildArch restriction

* Thu Sep 22 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl> - 0.4.12-2
- create separate subpackage for kernel module
- include man pages
- added kernel compatibility patch for kernels < 2.6.11

* Wed Aug 03 2005 Bastiaan Bakker <bastiaan.bakker@lifeline.nl>
- First version.


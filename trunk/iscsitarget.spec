##
## Global Package Definitions
##

## Package Definitions
# Revision number of the package
%define revision 1

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
Version: 0.4.17
Release: %{?revision: %{revision}}
License: GPL
Group: System Environment/Daemons
URL: http://sourceforge.net/projects/iscsitarget/
Packager: Iscsitarget Developer [iscsitarget-devel@lists.sourceforge.net]

## Source files
Source0: %{name}-%{version}.tar.gz

## Patches

## Install Requirements
Requires: kmod-%{name} = %{version}

## Build Requirements
BuildRequires: kernel >= 2.6
BuildRequires: %{ktype}-devel = %{kver}, gcc, /usr/bin/install, openssl-devel

## Build Definitions
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

## Description
%description
iSCSI Enterprise Target


##
## Kernel Module
##
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


##
## Package Creation
##

## Preparation
%prep


## Setup
%setup -q -n %{name}-%{version}


## Build
%build
%{__make} KERNELSRC=/lib/modules/%{kernel}/build


## Installation
%install
%{__rm} -rf %{buildroot}
%{__make} install-files KERNELSRC=/lib/modules/%{kernel}/build DISTDIR=%{buildroot}
mkdir -p %{buildroot}/etc/rc.d
mv %{buildroot}/etc/init.d %{buildroot}/etc/rc.d
rm -rf %{buildroot}/usr/share/doc/iscsitarget
rm -f %{buildroot}/lib/modules/%{kernel}/modules.*


## Cleaning
%clean
%{__rm} -rf %{buildroot}


## Post-Install Script
%post
/sbin/chkconfig --add iscsi-target


## Pre-Uninstall Script
%preun
if [ "$1" = 0 ]; then
    /sbin/service iscsi-target stop &>/dev/null
    /sbin/chkconfig --del iscsi-target
fi


## Post-Uninstall Script
%postun
if [ "$1" != 0 ]; then
    /sbin/service iscsi-target condrestart 2>&1 > /dev/null
fi


## Post-Install Script (Kernel Module)
%post -n kmod-%{name}
/sbin/depmod %{kernel} -A


## Pre-Uninstall Script (Kernel Module)
%preun -n kmod-%{name}
modprobe -r -q --set-version %{kernel} iscsi_trgt
/sbin/depmod %{kernel} -A


## File Catalog
%files
%defattr(-, root, root)
%{_sbindir}/*
%{_mandir}/man?/*
%{_sysconfdir}/rc.d/init.d/*
%config(noreplace) %{_sysconfdir}/ietd.conf
%config(noreplace) %{_sysconfdir}/initiators.allow
%config(noreplace) %{_sysconfdir}/initiators.deny
%doc ChangeLog COPYING README README.vmware


## File Catalog (Kernel Module)
%files -n kmod-%{name}
%defattr(-, root, root)
/lib/modules/%{kernel}/kernel/iscsi/iscsi_trgt.ko


%changelog
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


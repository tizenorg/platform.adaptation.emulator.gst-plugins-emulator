Name: gst-plugins-emulator
Version: 0.2.6
Release: 0
Summary: GStreamer Decoder and Encoder Plugins for Emulator
Group: Multimedia/Libraries
License: LGPL-2.0+
Source0: %{name}-%{version}.tar.gz
Source1001: packaging/%{name}.manifest
BuildRequires:  pkgconfig(gstreamer-0.10)
BuildRequires:  pkgconfig(gstreamer-plugins-base-0.10)
BuildRequires:  pkgconfig(glib-2.0)

%description
It includes video/audio decoders and encoders for Emulator
Its codec set is determined after communicating with emulator

%prep

%setup -q

%build
cp %{SOURCE1001} .
./autogen.sh
%configure --disable-static \
 --disable-nls \
 --prefix=%{_prefix} \

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}

%make_install
mkdir -p %{buildroot}/usr/share/license
cp COPYING.LIB %{buildroot}/usr/share/license/%{name}

%clean
rm -rf %{buildroot}

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%manifest gst-plugins-emulator.manifest
%defattr(-,root,root,-)
%{_libdir}/gstreamer-0.10/libgstemul.so
/usr/share/license/%{name}

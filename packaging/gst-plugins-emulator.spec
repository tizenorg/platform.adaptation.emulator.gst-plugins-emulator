Name: gst-plugins-emulator
Version: 0.1.12
Release: 2 
Summary: GStreamer Streaming-media framework plug-in for Tizen emulator.
Group: TO_BE/FILLED_IN
License: LGPLv2+
URL: http://gstreamer.net/
Source0: %{name}-%{version}.tar.gz
Source1001: packaging/%{name}.manifest
BuildRequires:  gettext
BuildRequires:  which
BuildRequires:  gstreamer-tools
BuildRequires:  gst-plugins-base-devel
BuildRequires:  pkgconfig(gstreamer-0.10)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(zlib)

%description

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

%clean
rm -rf %{buildroot}

%post

%postun

%files
%manifest gst-plugins-emulator.manifest
%defattr(-,root,root,-)
%{_libdir}/gstreamer-0.10/libgstemul.so

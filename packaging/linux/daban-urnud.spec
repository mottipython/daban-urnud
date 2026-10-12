Name:           daban-urnud
Version:        %{version}
Release:        1%{?dist}
Summary:        The Daban Urnud, a screensaver from "Anathem"
License:        MIT
Source0:        daban-urnud-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
The Geometers' ship from Neal Stephenson's novel Anathem, changing course:
it turns, lets atomic propulsion charges out through its pusher plate one
after another, and rides the blasts on its shock absorbers, while the orbs go
round the Core inside.

On Wayland desktops it starts by itself after five minutes idle.
daban-urnud-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/daban-urnud

%files
%license LICENSE
%doc README.md
%{_bindir}/daban-urnud
%{_bindir}/daban-urnud-screensaver
%{_datadir}/applications/daban-urnud.desktop
%{_datadir}/icons/hicolor/256x256/apps/daban-urnud.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/daban-urnud-screensaver.desktop

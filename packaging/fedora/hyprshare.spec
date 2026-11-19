Name:           hyprshare
Version:        0.6.0
Release:        1%{?dist}
Summary:        Linux-native sharing platform for Hyprland and Wayland

License:        MIT
URL:            https://github.com/yourusername/hyprshare
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.28
BuildRequires:  ninja-build
BuildRequires:  openssl-devel
BuildRequires:  sdbus-cpp-devel
BuildRequires:  pipewire-devel

%description
hyprshare is a serious, Linux-native sharing platform for Hyprland and Wayland
environments. It aims to unify screen sharing, capability sharing, diagnostics,
compatibility knowledge, and long-term peer-sharing workflows behind a coherent
CLI-first architecture.

%prep
%autosetup -n %{name}-%{version}

%build
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=%{_prefix}
cmake --build build

%check
ctest --test-dir build --output-on-failure

%install
DESTDIR=%{buildroot} cmake --install build
install -Dm644 data/systemd/hyprshare-daemon.service %{buildroot}%{_userunitdir}/hyprshare-daemon.service
install -Dm644 data/systemd/hyprshare-daemon.socket %{buildroot}%{_userunitdir}/hyprshare-daemon.socket

%files
%license LICENSE
%doc README.md
%{_bindir}/hyprshare
%{_bindir}/hyprshared
%{_userunitdir}/hyprshare-daemon.service
%{_userunitdir}/hyprshare-daemon.socket

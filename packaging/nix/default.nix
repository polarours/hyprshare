{ lib
, stdenv
, fetchFromGitHub
, cmake
, ninja
, openssl
, sdbus-cpp
, pipewire
}:

stdenv.mkDerivation rec {
  pname = "hyprshare";
  version = "0.6.0";

  src = fetchFromGitHub {
    owner = "yourusername";
    repo = "hyprshare";
    rev = "v${version}";
    hash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };

  nativeBuildInputs = [ cmake ninja ];
  buildInputs = [ openssl sdbus-cpp pipewire ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DHYPRSHARE_BUILD_TESTS=OFF"
  ];

  meta = with lib; {
    description = "Linux-native sharing platform for Hyprland and Wayland";
    homepage = "https://github.com/yourusername/hyprshare";
    license = licenses.mit;
    platforms = platforms.linux;
    maintainers = [ ];
  };
}

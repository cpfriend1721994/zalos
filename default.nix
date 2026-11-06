{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation rec {
  pname = "zalos";
  version = "1.0.6";

  # Only sources: never sessions (zsess.json), offline caches (.local/.config/.cache written by containers with
  # HOME=/workspace), screenshots, build trees or packages — the Nix store is world-readable.
  src = pkgs.lib.cleanSourceWith {
    src = ./.;
    filter = path: type:
      let
        base = baseNameOf (toString path);
        privateDirs = [ "build" "build-appimage" "dist" ".git" ".cache" ".config" ".local" ".flatpak-builder" "__pycache__" ];
      in
      !(type == "directory" && builtins.elem base privateDirs) &&
      !(type == "regular" && (base == "zsess.json" || pkgs.lib.hasSuffix ".png" base && pkgs.lib.hasPrefix "zalos_" base)) &&
      !(type == "regular" && pkgs.lib.hasSuffix ".AppImage" base) &&
      !(type == "regular" && pkgs.lib.hasSuffix ".rpm" base) &&
      !(type == "regular" && pkgs.lib.hasSuffix ".flatpak" base) &&
      !(type == "regular" && pkgs.lib.hasSuffix ".tar.gz" base);
  };

  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = with pkgs; [
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtsvg
    qt6.qtimageformats # WebP: animated stickers
    qt6.qtwayland
    qt6.qtwebsockets
    qt6.qtmultimedia
    zlib
    openssl
    xz
    sqlite
    openh264 # headers at build time; the library is dlopen()ed through ZALOS_OPENH264 below
  ];

  # NixOS has no global library path for dlopen("libopenh264.so.N"): point the app at the store copy.
  # The bundled zcall-native call engine (share/zalos/zcall-native) runs on node + python3, plays/records with
  # parec/pacat (pulseaudio tools, PipeWire-compatible) and dlopen()s libopus.so.0 from python (ctypes).
  qtWrapperArgs = [
    "--set-default ZALOS_OPENH264 ${pkgs.openh264}/lib/libopenh264.so"
    "--prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.nodejs pkgs.python3 pkgs.pulseaudio ]}"
    "--prefix LD_LIBRARY_PATH : ${pkgs.lib.makeLibraryPath [ pkgs.libopus ]}"
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DBUILD_TESTING=OFF"
  ] ++ pkgs.lib.optional pkgs.stdenv.hostPlatform.isAarch64 "-DZALOS_QML_AOT=OFF";

  meta = with pkgs.lib; {
    description = "Native Qt6 (C++/QML) desktop client for Zalo on Linux";
    homepage = "https://github.com/cpfriend1721994/zalos";
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "zalos";
  };
}


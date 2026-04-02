{
  description = "Local offline voice input plugin for Fcitx5";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    sherpa-onnx.url = "github:xifan2333/sherpa-onnx-flake";
    sherpa-onnx.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    {
      self,
      nixpkgs,
      sherpa-onnx,
    }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        # "i686-linux" # broken because of onnxruntime
        # "x86_64-darwin"
        # "aarch64-darwin"
      ];

      forAllSystems = nixpkgs.lib.genAttrs systems;
      pkgsFor = system: import nixpkgs { inherit system; };
      version = nixpkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION);
    in
    {
      inherit version;

      packages = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
          sherpa-deps = sherpa-onnx.packages."${pkgs.stdenv.hostPlatform.system}";

          vosk = pkgs.stdenv.mkDerivation {
            pname = "vosk-api";
            version = "0.3.50";
            src = pkgs.fetchurl {
              url = "https://archive.archlinux.org/packages/v/vosk-api/vosk-api-0.3.50-7-x86_64.pkg.tar.zst";
              sha256 = "80aae4295523c3849fd6f290882085976305ec8a3ad55a1a8211c4896b7a08b7";
            };
            nativeBuildInputs = [ pkgs.zstd ];
            unpackPhase = ''
              mkdir -p src
              tar -xf $src -C src
            '';
            installPhase = ''
              install -d $out/lib $out/include
              install -m 755 src/usr/lib/libvosk.so $out/lib/
              install -m 644 src/usr/include/vosk_api.h $out/include/
            '';
            dontFixup = true;
            meta.platforms = [ "x86_64-linux" ];
          };

          fcitx5-vinput = pkgs.stdenv.mkDerivation {
            pname = "fcitx5-vinput";
            inherit version;
            src = self;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
              gettext
              fcitx5
              qt6.wrapQtAppsHook
              autoPatchelfHook
            ];

            buildInputs = with pkgs; [
              fcitx5
              systemdLibs
              curl
              libarchive
              openssl
              pipewire
              onnxruntime
              qt6.qtbase
              cli11
              sherpa-deps.sherpa-onnx
              sherpa-deps.nlohmann_json
              clang
              mold
            ] ++ pkgs.lib.optionals (system == "x86_64-linux") [ vosk ];

            cmakeFlags = [
              "-DVINPUT_FETCH_CLI11=OFF"
              "-DCMAKE_BUILD_TYPE=Release"
              "-DCMAKE_C_COMPILER=clang"
              "-DCMAKE_CXX_COMPILER=clang++"
              "-DCMAKE_LINKER=mold"
              "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold"
              "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=mold"

            ];

            postInstall = ''
              rm -f $out/lib/fcitx5-vinput/libonnxruntime.so
            '';
          };
        in
        {
          inherit fcitx5-vinput vosk;
          default = fcitx5-vinput;
        }
      );
      devShells = forAllSystems (
        system:
        let
          pkgs = pkgsFor system;
          sherpa-deps = sherpa-onnx.packages."${pkgs.stdenv.hostPlatform.system}";
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              cmake
              fcitx5
              pkg-config
              gettext
              systemdLibs
              curl
              libarchive
              openssl
              onnxruntime
              pipewire
              qt6.qtbase
              qt6.wrapQtAppsHook
              cli11
              sherpa-deps.sherpa-onnx
              sherpa-deps.nlohmann_json
            ] ++ pkgs.lib.optionals (system == "x86_64-linux") [ self.packages.${system}.vosk ];
          };
        }
      );
    };
}

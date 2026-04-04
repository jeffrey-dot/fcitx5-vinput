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

          openfst-vosk-src = pkgs.fetchFromGitHub {
            owner = "alphacep";
            repo = "openfst";
            rev = "18e94e63870ebcf79ebb42b7035cd3cb626ec090";
            sha256 = "sha256-059BDNHbnim/rIMAaJ/+mD698R6chCdddOYVEtaYgfc=";
          };

          kaldi-vosk = pkgs.kaldi.overrideAttrs (old: {
            version = "0.3.50-vosk";
            src = pkgs.fetchFromGitHub {
              owner = "alphacep";
              repo = "kaldi";
              rev = "bc5baf14231660bd50b7d05788865b4ac6c34481";
              sha256 = "sha256-nFIKzBRZ6Og0Oj1wuYRMN33e1uZli5OLZSdnjUIybfg=";
            };
            nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [
              pkgs.clang
              pkgs.mold
            ];
            cmakeFlags = (old.cmakeFlags or [ ]) ++ [
              "-DFETCHCONTENT_SOURCE_DIR_OPENFST:PATH=${openfst-vosk-src}"
              "-DCMAKE_C_FLAGS=-I${openfst-vosk-src}/src/include"
              "-DCMAKE_CXX_FLAGS=-I${openfst-vosk-src}/src/include"
              "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold"
              "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=mold"
              "-DCMAKE_C_COMPILER=clang"
              "-DCMAKE_CXX_COMPILER=clang++"
            ];
            patches = (old.patches or [ ]) ++ [
              ./patches/kaldi-openfst-ngram.patch
            ];

            postPatch = (old.postPatch or "") + ''
              substituteInPlace CMakeLists.txt \
                --replace "@OPENFST_ROOT@" "${openfst-vosk-src}"
            '';
          });

          vosk = pkgs.stdenv.mkDerivation {
            pname = "vosk-api";
            version = "0.3.50";
            src = pkgs.fetchurl {
              url = "https://github.com/alphacep/vosk-api/archive/refs/tags/v0.3.50.tar.gz";
              sha256 = "03zp4h8a94q2wb76bjgp8ji6nqxcyb55fhldygss5jcrqny6f46c";
            };

            nativeBuildInputs = [
              pkgs.pkg-config
              pkgs.clang
              pkgs.mold
            ];

            buildInputs = [
              kaldi-vosk
            ];

            dontConfigure = true;

            buildPhase = ''
              runHook preBuild
              cd src
              make -j$NIX_BUILD_CORES \
                KALDI_ROOT=${kaldi-vosk} \
                OPENFST_ROOT=${kaldi-vosk} \
                USE_SHARED=1 \
                HAVE_OPENBLAS_CLAPACK=0 \
                CXX=clang++ \
                EXTRA_CFLAGS="-I${kaldi-vosk}/include/kaldi -I${kaldi-vosk}/include/openfst" \
                EXTRA_LDFLAGS="-L${kaldi-vosk}/lib -Wl,-rpath,${kaldi-vosk}/lib -fuse-ld=mold"
              runHook postBuild
            '';

            installPhase = ''
              install -d $out/lib $out/include
              install -m 755 libvosk.so $out/lib/
              install -m 644 vosk_api.h $out/include/
            '';

            meta.platforms = [
              "x86_64-linux"
              "aarch64-linux"
            ];
          };

          mkFcitx5Vinput =
            {
              withVosk ? true,
            }:
            pkgs.stdenv.mkDerivation {
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

              buildInputs =
                with pkgs;
                [
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
                  nlohmann_json
                  clang
                  mold
                ]
                ++ pkgs.lib.optionals withVosk [ vosk ];

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
          fcitx5-vinput = mkFcitx5Vinput { withVosk = false; };
          fcitx5-vinput-vosk = mkFcitx5Vinput { withVosk = true; };
          inherit vosk kaldi-vosk;
          default = mkFcitx5Vinput { withVosk = false; };
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
            packages =
              with pkgs;
              [
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
                nlohmann_json
              ]
              ++ pkgs.lib.optionals true [ self.packages.${system}.vosk ];
          };

          no-vosk = pkgs.mkShell {
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
              nlohmann_json
            ];
          };
        }
      );
    };
}

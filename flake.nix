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
            ];

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
          inherit fcitx5-vinput;
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
            ];
          };
        }
      );
    };
}

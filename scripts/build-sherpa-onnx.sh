#!/usr/bin/env bash
# Build and install sherpa-onnx from source.
# Usage: build-sherpa-onnx.sh [version] [prefix]
set -euo pipefail

version="${1:-1.12.28}"
prefix="${2:-/usr}"

workdir="$(mktemp -d)"
trap 'rm -rf "${workdir}"' EXIT

curl -sL "https://github.com/k2-fsa/sherpa-onnx/archive/refs/tags/v${version}.tar.gz" \
    | tar -xz -C "${workdir}"

cmake -S "${workdir}/sherpa-onnx-${version}" -B "${workdir}/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${prefix}" \
    -DSHERPA_ONNX_ENABLE_TESTS=OFF \
    -DSHERPA_ONNX_ENABLE_PYTHON=OFF \
    -DSHERPA_ONNX_ENABLE_C_API=ON \
    -DSHERPA_ONNX_ENABLE_BINARY=OFF \
    -DSHERPA_ONNX_ENABLE_TTS=OFF \
    -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF \
    -DBUILD_SHARED_LIBS=ON

cmake --build "${workdir}/build"
cmake --install "${workdir}/build"

#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <ubuntu24.04|debian12|fedora43|archlinux>" >&2
    exit 1
fi

target=$1
sudo_cmd=()
if [[ ${EUID} -ne 0 ]]; then
    sudo_cmd=(sudo)
fi

install_qt_apt() {
    if apt-cache show qt6-base-dev >/dev/null 2>&1; then
        "${sudo_cmd[@]}" apt-get install -y \
            qt6-base-dev \
            qt6-tools-dev \
            qt6-tools-dev-tools
    else
        "${sudo_cmd[@]}" apt-get install -y \
            qtbase5-dev \
            qttools5-dev
    fi
}

install_qt_dnf() {
    if dnf info qt6-qtbase-devel >/dev/null 2>&1; then
        "${sudo_cmd[@]}" dnf install -y \
            qt6-qtbase-devel \
            qt6-qttools-devel
    else
        "${sudo_cmd[@]}" dnf install -y \
            qt5-qtbase-devel \
            qt5-qttools-devel
    fi
}

install_qt_pacman() {
    if pacman -Si qt6-base >/dev/null 2>&1; then
        pacman -S --noconfirm --needed \
            qt6-base \
            qt6-tools
    else
        pacman -S --noconfirm --needed \
            qt5-base \
            qt5-tools
    fi
}

case "${target}" in
    ubuntu24.04)
        "${sudo_cmd[@]}" apt-get update
        "${sudo_cmd[@]}" apt-get install -y \
            clang \
            cmake \
            mold \
            ninja-build \
            pkg-config \
            sccache \
            gettext \
            curl \
            zstd \
            libcurl4-openssl-dev \
            libssl-dev \
            libarchive-dev \
            libpipewire-0.3-dev \
            libsystemd-dev \
            libfcitx5core-dev \
            libfcitx5config-dev \
            libfcitx5utils-dev \
            fcitx5-modules-dev \
            nlohmann-json3-dev
        install_qt_apt
        ;;
    debian12)
        apt-get update
        apt-get install -y \
            bzip2 \
            clang \
            cmake \
            mold \
            ninja-build \
            pkg-config \
            sccache \
            gettext \
            file \
            git \
            curl \
            zstd \
            libcurl4-openssl-dev \
            libssl-dev \
            libarchive-dev \
            libpipewire-0.3-dev \
            libsystemd-dev \
            libfcitx5core-dev \
            libfcitx5config-dev \
            libfcitx5utils-dev \
            fcitx5-modules-dev \
            nlohmann-json3-dev
        install_qt_apt
        ;;
    fedora43)
        "${sudo_cmd[@]}" dnf install -y \
            clang \
            cmake \
            mold \
            ninja-build \
            pkgconf-pkg-config \
            sccache \
            gettext \
            curl \
            zstd \
            libcurl-devel \
            openssl-devel \
            libarchive-devel \
            pipewire-devel \
            systemd-devel \
            fcitx5-devel \
            nlohmann-json-devel \
            cli11-devel
        install_qt_dnf
        ;;
    archlinux)
        pacman -Syu --noconfirm --needed \
            cli11 \
            clang \
            cmake \
            curl \
            fcitx5 \
            git \
            libarchive \
            mold \
            ninja \
            nlohmann-json \
            openssl \
            pipewire \
            pkgconf \
            sccache \
            systemd
        install_qt_pacman
        ;;
    *)
        echo "unsupported target: ${target}" >&2
        exit 1
        ;;
esac

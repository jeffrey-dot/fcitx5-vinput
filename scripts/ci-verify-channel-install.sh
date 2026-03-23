#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <ppa-ubuntu24.04|copr-fedora43|aur-archlinux> <version>" >&2
    exit 1
fi

target=$1
version=$2

check_no_missing_libs() {
    local binary=$1
    local report

    report=$(mktemp)
    ldd "${binary}" | tee "${report}"
    if grep -Fq "not found" "${report}"; then
        echo "missing shared library dependency detected for ${binary}" >&2
        exit 1
    fi
}

assert_common_install() {
    local addon_path
    local runtime_dir

    test -x /usr/bin/vinput
    test -x /usr/bin/vinput-daemon
    test -x /usr/bin/vinput-gui
    test -f /usr/share/fcitx5/addon/vinput.conf
    test -f /usr/share/systemd/user/vinput-daemon.service
    test -f /usr/share/dbus-1/services/org.fcitx.Vinput.service
    test -f /usr/share/fcitx5-vinput/default-config.json

    addon_path=$(find /usr/lib /usr/lib64 -path '*/fcitx5/fcitx5-vinput.so' -print -quit 2>/dev/null || true)
    runtime_dir=$(find /usr/lib /usr/lib64 -path '*/fcitx5-vinput' -type d -print -quit 2>/dev/null || true)

    if [[ -z "${addon_path}" ]]; then
        echo "fcitx5 addon was not installed" >&2
        exit 1
    fi

    if [[ -z "${runtime_dir}" ]]; then
        echo "bundled runtime directory was not installed" >&2
        exit 1
    fi

    test -f "${runtime_dir}/libsherpa-onnx-c-api.so"
    test -f "${runtime_dir}/libsherpa-onnx-cxx-api.so"

    vinput --help >/dev/null
    vinput-daemon --help >/dev/null

    check_no_missing_libs /usr/bin/vinput
    check_no_missing_libs /usr/bin/vinput-daemon
    check_no_missing_libs "${addon_path}"
}

case "${target}" in
    ppa-ubuntu24.04)
        export DEBIAN_FRONTEND=noninteractive
        expected_version="${version}-1ppa1~noble1"

        apt-get update
        apt-get install -y --no-install-recommends software-properties-common
        add-apt-repository -y ppa:xifan233/ppa
        apt-get update
        apt-cache policy fcitx5-vinput
        apt-get install -y --no-install-recommends "fcitx5-vinput=${expected_version}"
        dpkg-query -W -f='${Version}\n' fcitx5-vinput | grep -Fx "${expected_version}"
        ;;
    copr-fedora43)
        dnf install -y dnf-plugins-core
        dnf copr enable -y xifan/fcitx5-vinput-bin
        dnf install -y fcitx5-vinput
        rpm -q --qf '%{VERSION}\n' fcitx5-vinput | grep -Fx "${version}"
        ;;
    aur-archlinux)
        workspace=/tmp/aur-verify

        pacman -Sy --noconfirm --needed \
            cli11 \
            cmake \
            curl \
            fcitx5 \
            git \
            libarchive \
            ninja \
            nlohmann-json \
            openssl \
            pipewire \
            pkgconf \
            qt5-base \
            qt5-tools \
            systemd

        useradd -m builder
        rm -rf "${workspace}"
        install -d -o builder -g builder "${workspace}"
        su builder -c "git clone --depth 1 https://aur.archlinux.org/fcitx5-vinput-bin.git '${workspace}/fcitx5-vinput-bin'"
        su builder -c "grep -E '^pkgver=${version}$' '${workspace}/fcitx5-vinput-bin/PKGBUILD'"
        su builder -c "cd '${workspace}/fcitx5-vinput-bin' && makepkg --noconfirm --cleanbuild --force --nodeps"
        pacman -U --noconfirm "${workspace}/fcitx5-vinput-bin/"*.pkg.tar.zst
        pacman -Q fcitx5-vinput-bin | awk '{print $2}' | grep -E "^${version}-"
        ;;
    *)
        echo "unsupported target: ${target}" >&2
        exit 1
        ;;
esac

assert_common_install

#!/usr/bin/env bash

# Pre-built vosk-api from Arch Linux official package (extra repo).
# The upstream GitHub releases only ship v0.3.45 binaries which lack
# the endpointer API; the Arch package builds v0.3.50 from source.

vosk_set_vars() {
    local version="${1:?version is required}"
    local pkgrel="${2:-7}"

    VOSK_VERSION="${version}"
    VOSK_PKGREL="${pkgrel}"
    VOSK_ARCHIVE="vosk-api-${version}-${pkgrel}-x86_64.pkg.tar.zst"
    VOSK_URL="https://geo.mirror.pkgbuild.com/extra/os/x86_64/${VOSK_ARCHIVE}"
    VOSK_SHA256=""

    case "${version}-${pkgrel}" in
        0.3.50-7)
            VOSK_SHA256="80aae4295523c3849fd6f290882085976305ec8a3ad55a1a8211c4896b7a08b7"
            ;;
    esac
}

vosk_set_vars "${VOSK_VERSION:-0.3.50}" "${VOSK_PKGREL:-7}"

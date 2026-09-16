#!/usr/bin/env bash
# =====================================================================
#  scripts/ci/fetch.sh — download and unpack a pinned dependency source
#  SPDX-License-Identifier: GPL-3.0-only
#  Part of HobbyCAD (ayourk/hobbycad)
# =====================================================================
#
#  Usage: scripts/ci/fetch.sh <dependency> <destination-directory>
#
#  <dependency> is a versions.json name that carries a "source" entry
#  (opencascade, libslvs, nlohmann-json, libwebp). The archive's SHA-256
#  is checked against the pin before it is unpacked.
# =====================================================================
. "$(dirname "$0")/lib.sh"
[ $# -eq 2 ] || die "usage: $0 <dependency> <destination-directory>"
fetch_source "$1" "$2"

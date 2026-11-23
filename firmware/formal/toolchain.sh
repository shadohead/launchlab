#!/usr/bin/env bash
# Obtain exact official x86_64 Linux release assets for a GitHub-hosted runner.
set -euo pipefail
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || {
  echo "toolchain.sh expects Linux x86_64; on other hosts see firmware/formal/README.md." >&2
  exit 1
}
root="${1:?usage: toolchain.sh CACHE_DIR}"
mkdir -p "$root"
tmp="$(mktemp -d "$root/.download.XXXXXXXX")"
trap 'rm -rf "$tmp"' EXIT
check() { printf '%s  %s\n' "$2" "$1" | sha256sum --check --status; }
fetch() {
  local path="$1" digest="$2" url="$3" asset="$4"
  if [[ -f "$path" ]] && check "$path" "$digest"; then return; fi
  # The TLC release tag may roll; never accept bytes merely because the tag matches.
  # The release asset id is also pinned to the verified edition.
  if curl --fail --location --retry 2 --max-time 180 -sS -H 'Accept: application/octet-stream' "$asset" -o "$tmp/asset" &&
     check "$tmp/asset" "$digest"; then
    :
  else
    curl --fail --location --retry 2 --max-time 180 -sS "$url" -o "$tmp/asset"
    check "$tmp/asset" "$digest" || { echo "Checksum mismatch: $url" >&2; exit 1; }
  fi
  mv "$tmp/asset" "$path"
}
fetch "$root/tla2tools.jar" fdffef247f563ef6edbfd44958d2a82db79e7564e07cb673262d896353fa6713 \
  https://github.com/tlaplus/tlaplus/releases/download/v1.8.0/tla2tools.jar \
  https://api.github.com/repos/tlaplus/tlaplus/releases/assets/768714348
fetch "$root/lean-4.30.0-linux.tar.zst" 4dad74141c2c119ca1aa626656be83b8e14238afba97271fd7bf1eb3f081b319 \
  https://github.com/leanprover/lean4/releases/download/v4.30.0/lean-4.30.0-linux.tar.zst \
  https://api.github.com/repos/leanprover/lean4/releases/assets/430245352
# The cache contains verified archives, not writable executables from earlier jobs.
tar --zstd -xf "$root/lean-4.30.0-linux.tar.zst" -C "$tmp"
test -x "$tmp/lean-4.30.0-linux/bin/lean"
rm -rf "$root/lean-4.30.0-linux"
mv "$tmp/lean-4.30.0-linux" "$root/lean-4.30.0-linux"

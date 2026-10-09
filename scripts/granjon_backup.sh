#!/usr/bin/env bash
# Granjon backup -- zip the project for handing to a session.
#   ./scripts/granjon_backup.sh -o s001_spine    -> backups/s001_spine.zip
# Allowlist-based, so build artifacts can't ride along. Paths derive from this
# script's own location. Previous zips in backups/ retire to backups/Old/.
set -euo pipefail

name=""
while getopts "o:h" opt; do
    case "$opt" in
        o) name="$OPTARG" ;;
        *) echo "usage: $0 -o <sNNN_tagline>"; exit 1 ;;
    esac
done
[[ -n "$name" ]] || { echo "usage: $0 -o <sNNN_tagline>"; exit 1; }
name="${name%.zip}"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
backups="$root/backups"
mkdir -p "$backups/Old"

# Retire previous zips.
shopt -s nullglob
for z in "$backups"/*.zip; do mv -f "$z" "$backups/Old/"; done
shopt -u nullglob

# The allowlist: what a session needs, nothing it doesn't.
include=(
    docs
    scripts
    fixes
    granjon/include
    granjon/src
    granjon/resources
    granjon/third_party
    granjon/tools
    granjon/CMakeLists.txt
    granjon/build.sh
    granjon/README.md
    granjon/.gitignore
)
present=()
for p in "${include[@]}"; do [[ -e "$root/$p" ]] && present+=("$p"); done

out="$backups/$name.zip"
( cd "$root" && zip -qr "$out" "${present[@]}" -x '*/build/*' '*.o' '*.a' '*/.cache/*' )
echo "-> $out  ($(du -h "$out" | cut -f1))"

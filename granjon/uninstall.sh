#!/usr/bin/env bash
# Granjon uninstall (r10). Removes what ./install.sh put in (its list:
# <prefix>/share/granjon/install-manifest.txt). Your fonts are never touched.
#
#   ./uninstall.sh                from ~/.local
#   ./uninstall.sh --system       from /usr/local (asks for sudo)
#   ./uninstall.sh --prefix DIR   from where you installed it
#   ./uninstall.sh --purge        also remove your Granjon settings, recent
#                                 files list and preview cache:
#                                 ~/.config/granjon ~/.local/share/granjon ~/.cache/granjon
set -euo pipefail

prefix="$HOME/.local"
sudo=""
purge=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --system)  prefix="/usr/local"; sudo="sudo" ;;
        --prefix)  prefix="${2:?--prefix needs a folder}"; shift ;;
        --purge)   purge=1 ;;
        -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "uninstall.sh: unknown option $1 (try --help)" >&2; exit 2 ;;
    esac
    shift
done
[[ -n "$sudo" && "$(id -u)" == 0 ]] && sudo=""

manifest="$prefix/share/granjon/install-manifest.txt"
if [[ -f "$manifest" ]]; then
    mapfile -t files < "$manifest"
else
    echo "No install list at $manifest -- removing the three standard files."
    files=("$prefix/bin/granjon"
           "$prefix/share/applications/io.github.granjon.Granjon.desktop"
           "$prefix/share/icons/hicolor/scalable/apps/io.github.granjon.Granjon.svg")
fi

removed=0
for f in "${files[@]}"; do
    [[ -z "$f" ]] && continue
    case "$f" in   # only what install.sh writes, only under the prefix
        "$prefix"/bin/granjon|"$prefix"/share/applications/io.github.granjon.Granjon.desktop|\
        "$prefix"/share/icons/hicolor/scalable/apps/io.github.granjon.Granjon.svg|"$manifest") ;;
        *) echo "  skipped (not a Granjon file): $f"; continue ;;
    esac
    if [[ -e "$f" ]]; then
        $sudo rm -f "$f" && echo "  removed $f" && removed=$((removed + 1))
    fi
done
# the list's folder, if nothing else is in it (recent.json lives there under ~/.local)
$sudo rmdir "$prefix/share/granjon" 2>/dev/null || true

if command -v gtk-update-icon-cache >/dev/null && [[ -f "$prefix/share/icons/hicolor/index.theme" ]]; then
    $sudo gtk-update-icon-cache -q -t -f "$prefix/share/icons/hicolor" || true
fi
command -v update-desktop-database >/dev/null && [[ -d "$prefix/share/applications" ]] && \
    { $sudo update-desktop-database -q "$prefix/share/applications" || true; }

if [[ $purge == 1 ]]; then
    for d in "${XDG_CONFIG_HOME:-$HOME/.config}/granjon" "${XDG_DATA_HOME:-$HOME/.local/share}/granjon" \
             "${XDG_CACHE_HOME:-$HOME/.cache}/granjon"; do
        [[ -d "$d" ]] && rm -rf "$d" && echo "  removed $d"
    done
fi

if [[ $removed == 0 ]]; then
    echo "Granjon was not installed in $prefix."
else
    echo "Granjon removed from $prefix.$([[ $purge == 0 ]] && echo " Settings kept (--purge removes them).")"
fi

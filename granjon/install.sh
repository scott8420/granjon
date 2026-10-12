#!/usr/bin/env bash
# Granjon install (r10). Builds a Release copy, runs the selftest, installs
# three files -- the program, its launcher entry, the app icon -- and keeps a
# list of them for ./uninstall.sh.
#
#   ./install.sh                  into ~/.local (no sudo)       -> ~/.local/bin/granjon
#   ./install.sh --system         into /usr/local (asks for sudo) -> /usr/local/bin/granjon
#   ./install.sh --prefix DIR     anywhere else
#   ./install.sh --no-test        skip the selftest
#
# Deps: as build.sh (gtkmm4.0-devel spdlog-devel libxml2-devel brotli-devel
# cmake gcc-c++). The Release build lives in build-install/, apart from your
# working build/. Run it again after pulling a new zip: it replaces the files.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

prefix="$HOME/.local"
sudo=""
test=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --system)  prefix="/usr/local"; sudo="sudo" ;;
        --prefix)  prefix="${2:?--prefix needs a folder}"; shift ;;
        --no-test) test=0 ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install.sh: unknown option $1 (try --help)" >&2; exit 2 ;;
    esac
    shift
done
[[ -n "$sudo" && "$(id -u)" == 0 ]] && sudo=""   # already root

echo "== Building (Release, build-install/)"
log=build-install.log
targets=(--target granjon)
[[ $test == 1 ]] && targets+=(granjon_selftest)
if ! { cmake -S . -B build-install -DCMAKE_BUILD_TYPE=Release && \
       cmake --build build-install "${targets[@]}" -- -j"$(nproc)"; } >"$log" 2>&1; then
    grep -E "error|Error" "$log" | head -20
    echo "install.sh: the build failed -- nothing installed (whole output: $log)." >&2
    exit 1
fi
echo "   built (output in $log)"

if [[ $test == 1 ]]; then
    echo "== Selftest"
    if ! out=$(./build-install/granjon_selftest 2>&1); then
        echo "$out" | grep -E "FAIL" | head -20
        echo "$out" | tail -1
        echo "install.sh: the selftest failed -- nothing installed." >&2
        exit 1
    fi
    echo "   $(echo "$out" | tail -1)"
fi

echo "== Installing into $prefix"
$sudo cmake --install build-install --prefix "$prefix" --component granjon --strip

# The launcher runs the program by its full path (a desktop session's PATH
# may not hold ~/.local/bin).
desktop="$prefix/share/applications/io.github.granjon.Granjon.desktop"
$sudo sed -i "s|^Exec=.*|Exec=$prefix/bin/granjon|" "$desktop"

# What went where, for uninstall.sh.
manifest="$prefix/share/granjon/install-manifest.txt"
$sudo mkdir -p "$(dirname "$manifest")"
{ cat build-install/install_manifest_granjon.txt; echo; echo "$manifest"; } | sed '/^$/d' | $sudo tee "$manifest" >/dev/null

# Let the desktop see the new entry and icon now.
if command -v gtk-update-icon-cache >/dev/null && [[ -f "$prefix/share/icons/hicolor/index.theme" ]]; then
    $sudo gtk-update-icon-cache -q -t -f "$prefix/share/icons/hicolor" || true
fi
command -v update-desktop-database >/dev/null && { $sudo update-desktop-database -q "$prefix/share/applications" || true; }

echo ""
echo "Installed:"
sed 's/^/  /' "$manifest"
case ":$PATH:" in
    *":$prefix/bin:"*) echo "Run: granjon   (or find Granjon in the app grid)" ;;
    *) echo "Run: $prefix/bin/granjon   (or find Granjon in the app grid; $prefix/bin is not on your PATH)" ;;
esac
echo "Remove: ./uninstall.sh${sudo:+ --system}$([[ "$prefix" != "$HOME/.local" && "$prefix" != /usr/local ]] && echo " --prefix $prefix")"

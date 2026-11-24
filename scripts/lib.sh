# Shared paths and helpers for the scripts in this directory. Source it, do not run it:
#   . "$(dirname "$(readlink -f "$0")")/lib.sh"
#
# Every path can be overridden from the environment:
#   PIMAX_SRC     upstream checkouts (monado-rust, basalt, envision-src, pimax-configs)   default: <repo>/src
#   PIMAX_PREFIX  user install prefix for Monado, Basalt and the tools                     default: ~/.local
#   PIMAX_CACHE   build trees for Envision, logs, test results                             default: ~/.cache/pimax-linux
#   PIMAX_WAYVR   WayVR AppImage                                                           default: ~/.local/opt/wayvr/WayVR-x86_64.AppImage

PIMAX_REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
PIMAX_SRC="${PIMAX_SRC:-$PIMAX_REPO/src}"
PIMAX_PREFIX="${PIMAX_PREFIX:-$HOME/.local}"
PIMAX_CACHE="${PIMAX_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/pimax-linux}"
PIMAX_WAYVR="${PIMAX_WAYVR:-$HOME/.local/opt/wayvr/WayVR-x86_64.AppImage}"
PIMAX_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# Directory under PREFIX that holds a library: lib64 (Fedora, Arch), lib/<multiarch> (Debian, Ubuntu) or lib.
# Usage: pimax_libdir PREFIX [LIBNAME]   (LIBNAME defaults to libmonado.so)
pimax_libdir() {
    local prefix=$1 name=${2:-libmonado.so} d
    for d in lib64 "lib/$(gcc -print-multiarch 2>/dev/null)" lib; do
        [ -n "$d" ] && [ "$d" != "lib/" ] && compgen -G "$prefix/$d/$name*" >/dev/null && { echo "$prefix/$d"; return 0; }
    done
    return 1
}

# Include directory of the installed clang, needed by bindgen when building the Rust driver.
pimax_clang_include() {
    local dir
    dir=$(clang -print-resource-dir 2>/dev/null) && [ -d "$dir/include" ] && { echo "$dir/include"; return 0; }
    dir=$(ls -d /usr/lib/clang/*/include /usr/lib64/clang/*/include /usr/lib/llvm-*/lib/clang/*/include 2>/dev/null | sort -V | tail -1)
    [ -n "$dir" ] && echo "$dir"
}

# Kill processes by exact PID (one or more, whitespace-separated); never by name pattern.
pimax_stop_pid() { local p; for p in $*; do kill "$p" 2>/dev/null; done; return 0; }

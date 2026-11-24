#!/bin/bash
# One-command install for the Pimax Crystal Super setup: from a fresh clone to a working user install.
#
#   scripts/install.sh [--check] [--no-envision] [--udev] [--system]
#
# default        check build deps, clone/fetch the pinned sources (sources.lock) into PIMAX_SRC, apply
#                patches/monado on branch pimax-linux, patches/basalt and patches/envision on branch pimax, build and install
#                Basalt + Monado + Envision + tools into PIMAX_PREFIX (Envision into PIMAX_PREFIX/opt/envision-pimax),
#                copy headset configs, the WirePlumber rule and the environment.d file.
# --check        only report missing build dependencies and exit (0 = nothing missing, 1 = something missing)
# --no-envision  skip Envision
# --udev         also install config/99-*.rules into /etc/udev/rules.d (sudo) and reload udev
# --system       also install Monado and Basalt into /usr (sudo; Envision runs /usr/bin/monado-service)
#
# Never installs packages, never uses sudo unless --udev/--system. Safe to re-run: existing checkouts are reused,
# builds are incremental. A checkout with uncommitted changes or commits this script does not know stops the run.
#
# Paths (see lib.sh): PIMAX_SRC, PIMAX_PREFIX, PIMAX_CACHE. Also honoured:
#   PIMAX_JOBS     parallel build jobs (default: nproc)
#   PIMAX_DEVROOT  extra root with development files (<root>/usr/lib64/pkgconfig ...), e.g. -devel packages
#                  unpacked without root; default $PIMAX_CACHE/devroot if it exists
#   XDG_CONFIG_HOME  where the headset configs, WirePlumber rule and environment.d file go (default ~/.config)
set -euo pipefail
. "$(dirname "$(readlink -f "$0")")/lib.sh"

CHECK_ONLY=0 ENVISION=1 UDEV=0 SYSTEM=0
for a in "$@"; do
    case $a in
        --check) CHECK_ONLY=1 ;;
        --no-envision) ENVISION=0 ;;
        --udev) UDEV=1 ;;
        --system) SYSTEM=1 ;;
        -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $a (see --help)" >&2; exit 2 ;;
    esac
done

CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
JOBS="${PIMAX_JOBS:-$(nproc)}"
LDCONFIG=$(command -v ldconfig || echo /sbin/ldconfig)
LOCK="$PIMAX_REPO/sources.lock"
MONADO_SRC="$PIMAX_SRC/monado-rust"
BASALT_SRC="$PIMAX_SRC/basalt"
ENVISION_SRC="$PIMAX_SRC/envision-src"
CONFIGS_SRC="$PIMAX_SRC/pimax-configs"
BASALT_BUILD="$PIMAX_CACHE/basalt-build"
MONADO_BUILD="$PIMAX_CACHE/monado-build"
ENVISION_BUILD="$PIMAX_CACHE/envision-build"
ENVISION_PREFIX="$PIMAX_PREFIX/opt/envision-pimax"
MONADO_BRANCH=pimax-linux
BASALT_BRANCH=pimax
ENVISION_BRANCH=pimax

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '\033[33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------------------------------------------
# Pins from sources.lock: "<name> <url> [branch <branch>] <commit> (notes)"
# ---------------------------------------------------------------------------------------------------------------
declare -A PIN_URL PIN_BRANCH PIN_REV
read_pins() {
    [ -f "$LOCK" ] || die "missing $LOCK"
    local name url w3 w4 w5 rest
    while read -r name url w3 w4 w5 rest; do
        case $name in monado|basalt|pimax-configs|envision|pimax-protocol) ;; *) continue ;; esac
        PIN_URL[$name]=$url
        case $w3 in
            branch) PIN_BRANCH[$name]=$w4; PIN_REV[$name]=$w5 ;;
            rev)    PIN_BRANCH[$name]=;    PIN_REV[$name]=$w4 ;;
            *)      PIN_BRANCH[$name]=;    PIN_REV[$name]=$w3 ;;
        esac
        [[ ${PIN_REV[$name]} =~ ^[0-9a-f]{7,40}$ ]] || die "sources.lock: no commit for $name"
    done < <(grep -vE '^[[:space:]]*(#|$)' "$LOCK")
    local n
    for n in monado basalt pimax-configs envision; do
        [ -n "${PIN_URL[$n]:-}" ] || die "sources.lock: no line for $n"
    done
}

# ---------------------------------------------------------------------------------------------------------------
# Dependency check
# ---------------------------------------------------------------------------------------------------------------
# Extra development root and a private dir for unversioned .so links (cargo's openxr crate links -lopenxr_loader).
DEVROOT="${PIMAX_DEVROOT:-}"
[ -z "$DEVROOT" ] && [ -d "$PIMAX_CACHE/devroot/usr" ] && DEVROOT="$PIMAX_CACHE/devroot"
LINKDIR="$PIMAX_CACHE/link"
setup_build_env() {
    local d pc="" lib=""
    if [ -n "$DEVROOT" ]; then
        for d in "$DEVROOT"/usr/lib64 "$DEVROOT"/usr/lib/*-linux-gnu "$DEVROOT"/usr/lib; do
            [ -d "$d" ] || continue
            lib="$lib${lib:+:}$d"
            [ -d "$d/pkgconfig" ] && pc="$pc${pc:+:}$d/pkgconfig"
        done
        [ -d "$DEVROOT/usr/share/pkgconfig" ] && pc="$pc${pc:+:}$DEVROOT/usr/share/pkgconfig"
    fi
    [ -d "$LINKDIR" ] && lib="$LINKDIR${lib:+:}$lib"
    [ -n "$pc" ]  && export PKG_CONFIG_PATH="$pc${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    [ -n "$lib" ] && export LIBRARY_PATH="$lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
    # Corrosion or other CMake packages built from source into the prefix are found too.
    export CMAKE_PREFIX_PATH="$PIMAX_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    return 0
}

MISSING=()
# need <scope> <kind> <name> <fedora> <arch> <debian>
#   scope: core | envision ; kind: cmd | pc (name may be "mod>=ver") | cmake | so | header
need() {
    local scope=$1 kind=$2 name=$3 fed=$4 arch=$5 deb=$6 ok=1 mod ver d
    [ "$scope" = envision ] && [ "$ENVISION" = 0 ] && return 0
    case $kind in
        cmd) command -v "$name" >/dev/null 2>&1 || ok=0 ;;
        pc)  mod=${name%%>=*}; ver=; [ "$mod" != "$name" ] && ver=${name#*>=}
             if [ -n "$ver" ]; then pkg-config --atleast-version="$ver" "$mod" 2>/dev/null || ok=0
             else pkg-config --exists "$mod" 2>/dev/null || ok=0; fi ;;
        cmake) ok=0
             for d in /usr/lib64/cmake /usr/lib/cmake /usr/lib/*-linux-gnu/cmake /usr/share/cmake /usr/share \
                      /usr/local/lib64/cmake /usr/local/lib/cmake "$PIMAX_PREFIX"/lib64/cmake "$PIMAX_PREFIX"/lib/cmake; do
                 compgen -G "$d/${name}/${name}Config.cmake" >/dev/null || compgen -G "$d/${name,,}/${name}Config.cmake" >/dev/null \
                     || compgen -G "$d/${name,,}/${name,,}-config.cmake" >/dev/null || continue
                 ok=1; break
             done ;;
        so)  grep -q "[[:space:]]$name[. ]" <<<"$LDCACHE" || ok=0 ;;
        libclang) # Debian/Ubuntu keep the unversioned libclang.so only under /usr/lib/llvm-N/lib
             grep -qE "[[:space:]]libclang(-[0-9.]+)?\.so" <<<"$LDCACHE" || compgen -G "/usr/lib/llvm-*/lib/libclang.so*" >/dev/null || ok=0 ;;
        clanginc) [ -n "$(pimax_clang_include)" ] || ok=0 ;;
        header) ok=0; for d in /usr/include /usr/local/include; do [ -f "$d/$name" ] && { ok=1; break; }; done ;;
    esac
    [ $ok = 1 ] || MISSING+=("$(printf '%-9s %-26s Fedora: %-34s Arch: %-22s Debian/Ubuntu: %s' "[$scope]" "$name" "$fed" "$arch" "$deb")")
}

check_deps() {
    MISSING=()
    LDCACHE=$("$LDCONFIG" -p 2>/dev/null || true)
    command -v pkg-config >/dev/null 2>&1 || MISSING+=("$(printf '%-9s %-26s Fedora: %-34s Arch: %-22s Debian/Ubuntu: %s' "[core]" "pkg-config" "pkgconf-pkg-config" "pkgconf" "pkgconf")")
    need core cmd git               git                  git              git
    need core cmd cmake             cmake                cmake            cmake
    need core cmd ninja             ninja-build          ninja            ninja-build
    need core cmd cc                gcc                  gcc              build-essential
    need core cmd c++               gcc-c++              gcc              g++
    need core cmd cargo             cargo                rust             cargo
    need core cmd rustc             rust                 rust             rustc
    need core clanginc clang-resource-dir clang-libs           clang            libclang-dev
    need core libclang libclang.so       clang-libs           clang            libclang-dev
    need core cmd glslangValidator  glslang              glslang          glslang-tools
    need core cmd python3           python3              python           python3
    need core pc  eigen3            eigen3-devel         eigen            libeigen3-dev
    need core pc  vulkan            vulkan-loader-devel  vulkan-icd-loader libvulkan-dev
    need core header vulkan/vulkan.h vulkan-headers      vulkan-headers   libvulkan-dev
    need core pc  libudev           systemd-devel        systemd-libs     libudev-dev
    need core pc  libusb-1.0        libusb1-devel        libusb           libusb-1.0-0-dev
    need core pc  hidapi-libusb     hidapi-devel         hidapi           libhidapi-dev
    need core pc  opencv4           opencv-devel         opencv           libopencv-dev
    need core cmake Ceres           ceres-solver-devel   ceres-solver     libceres-dev
    need core cmake Corrosion       corrosion            corrosion        "(none: build github.com/corrosion-rs/corrosion, cmake --install into PIMAX_PREFIX)"
    need core pc  tbb               tbb-devel            onetbb           libtbb-dev
    need core pc  fmt               fmt-devel            fmt              libfmt-dev
    need core pc  epoxy             libepoxy-devel       libepoxy         libepoxy-dev
    need core pc  gl                mesa-libGL-devel     libglvnd         libgl-dev
    need core pc  egl               mesa-libEGL-devel    libglvnd         libegl-dev
    need core pc  x11               libX11-devel         libx11           libx11-dev
    need core pc  xrandr            libXrandr-devel      libxrandr        libxrandr-dev
    need core so  libopenxr_loader.so.1 openxr-libs      openxr           libopenxr-loader1
    need envision cmd meson         meson                meson            meson
    need envision cmd glib-compile-resources glib2-devel glib2            libglib2.0-dev-bin
    need envision cmd msgfmt        gettext              gettext          gettext
    need envision pc  "glib-2.0>=2.66" glib2-devel       glib2            libglib2.0-dev
    need envision pc  "gtk4>=4.10"  gtk4-devel           gtk4             libgtk-4-dev
    need envision pc  "libadwaita-1>=1.5" libadwaita-devel libadwaita     libadwaita-1-dev
    need envision pc  "vte-2.91-gtk4>=0.72" vte291-gtk4-devel vte4        libvte-2.91-gtk4-dev
    need envision pc  openssl       openssl-devel        openssl          libssl-dev
}

report_deps() {
    if [ ${#MISSING[@]} = 0 ]; then
        info "all build dependencies found"
        return 0
    fi
    echo "Missing build dependencies (install them with your package manager, then re-run):"
    printf '  %s\n' "${MISSING[@]}"
    if [ "$ENVISION" = 1 ] && printf '%s\n' "${MISSING[@]}" | grep -q '^\[envision\]'; then
        cat <<EOF
  Envision only needs the .pc files and unversioned .so links of gtk4/libadwaita/vte. Without root you can unpack
  the -devel packages into a directory and point PIMAX_DEVROOT at it (default: $PIMAX_CACHE/devroot if it
  exists), or skip Envision with --no-envision.
EOF
    fi
    return 1
}

# ---------------------------------------------------------------------------------------------------------------
# Checkouts
# ---------------------------------------------------------------------------------------------------------------
g() { git -C "$1" "${@:2}"; }

have_commit() { g "$1" rev-parse -q --verify "$2^{commit}" >/dev/null 2>&1; }

# Make sure commit $3 exists in repo $1 (fetch by sha, then by branch $2).
ensure_commit() {
    local dir=$1 branch=$2 rev=$3
    have_commit "$dir" "$rev" && return 0
    info "fetching $rev"
    g "$dir" fetch -q origin "$rev" 2>/dev/null || true
    have_commit "$dir" "$rev" && return 0
    [ -n "$branch" ] && g "$dir" fetch -q origin "+refs/heads/$branch:refs/remotes/origin/$branch" || true
    have_commit "$dir" "$rev" || die "$dir: pinned commit $rev not found upstream (branch ${branch:-?}); check sources.lock"
}

# Stop on uncommitted changes to tracked files (untracked files such as build/ are fine).
require_clean() {
    local dir=$1 changes
    changes=$(g "$dir" status --porcelain --untracked-files=no --ignore-submodules)
    [ -z "$changes" ] || die "$dir has uncommitted changes:
$changes
Commit or stash them (git -C '$dir' stash), then re-run. Nothing was changed."
}

# Clone if missing; existing dir must be a git checkout of the same project.
clone_or_reuse() {
    local name=$1 dir=$2 url=${PIN_URL[$1]} branch=${PIN_BRANCH[$1]}
    if [ ! -e "$dir" ]; then
        info "cloning $url -> $dir"
        mkdir -p "$(dirname "$dir")"
        # No --branch: the pin is a commit, fetched by hash if the branch moved on or was removed.
        git clone -q "$url" "$dir" || die "could not clone $url"
        return 0
    fi
    g "$dir" rev-parse --git-dir >/dev/null 2>&1 || die "$dir exists but is not a git checkout; move it away and re-run"
    local origin
    origin=$(g "$dir" remote get-url origin 2>/dev/null || true)
    [ "${origin%.git}" = "${url%.git}" ] || warn "$dir: origin is '$origin', sources.lock says '$url' (continuing)"
    info "reusing $dir"
}

# Plain pinned checkout (no patches): detached HEAD at the pin. Moves HEAD only if nothing would be lost.
checkout_pinned() {
    local name=$1 dir=$2 rev=${PIN_REV[$1]} head full unknown
    clone_or_reuse "$name" "$dir"
    require_clean "$dir"
    ensure_commit "$dir" "${PIN_BRANCH[$name]}" "$rev"
    full=$(g "$dir" rev-parse "$rev^{commit}")
    head=$(g "$dir" rev-parse HEAD)
    if [ "$head" != "$full" ]; then
        # commits reachable from HEAD that are on no remote branch would become hard to find
        unknown=$(g "$dir" rev-list HEAD --not --remotes "$full" | head -5)
        [ -z "$unknown" ] || die "$dir: HEAD has local commits that are not upstream:
$(g "$dir" log --oneline --no-walk $unknown)
Push or branch them away, or move the checkout aside, then re-run."
        info "checking out $name at ${full:0:12}"
        g "$dir" -c advice.detachedHead=false checkout -q --detach "$full"
    else
        info "$name at ${full:0:12}"
    fi
}

# patch-ids of a patch series directory, one per line, in order
series_ids() { local p; for p in "$1"/*.patch; do git patch-id --stable < "$p" | cut -d' ' -f1; done; }
branch_ids() {
    local dir=$1 base=$2 tip=$3 c
    local id
    for c in $(g "$dir" rev-list --reverse "$base..$tip"); do
        id=$(g "$dir" show --format=email "$c" | git patch-id --stable | cut -d' ' -f1)
        echo "${id:-no-diff-$c}"   # merges and empty commits have no patch-id: never match the series
    done
}

git_am() {
    local dir=$1; shift
    local id=()
    [ -n "$(g "$dir" config user.email || true)" ] || id+=(-c user.email=install@pimax-linux.invalid)
    [ -n "$(g "$dir" config user.name || true)" ]  || id+=(-c user.name="pimax-linux install")
    if ! git -C "$dir" "${id[@]}" am -q --committer-date-is-author-date "$@"; then
        git -C "$dir" am --abort 2>/dev/null || true
        die "$dir: a patch did not apply on the pinned base. The checkout was left on the base commit plus the
patches that did apply. Check sources.lock and the patches/ directory."
    fi
}

# Pinned base + patch series on a local work branch.
checkout_patched() {
    local name=$1 dir=$2 branch=$3 patches=$4 base=${PIN_REV[$1]} cur
    clone_or_reuse "$name" "$dir"
    require_clean "$dir"
    local gd
    gd=$(g "$dir" rev-parse --absolute-git-dir)
    [ ! -d "$gd/rebase-apply" ] && [ ! -d "$gd/rebase-merge" ] \
        || die "$dir: a git am/rebase is in progress; finish or abort it first"
    ensure_commit "$dir" "${PIN_BRANCH[$name]}" "$base"
    base=$(g "$dir" rev-parse "$base^{commit}")
    local -a want have
    mapfile -t want < <(series_ids "$patches")

    if ! g "$dir" rev-parse -q --verify "refs/heads/$branch" >/dev/null; then
        info "creating branch $branch at ${base:0:12}"
        cur=$(g "$dir" symbolic-ref -q --short HEAD || echo "(detached)")
        if [ "$cur" = "(detached)" ] || [ "$(g "$dir" rev-parse HEAD)" != "$base" ]; then
            local unknown
            unknown=$(g "$dir" rev-list HEAD --not --remotes --branches "$base" | head -5)
            [ -z "$unknown" ] || die "$dir: detached HEAD has commits on no branch; create a branch for them first"
        fi
        g "$dir" checkout -q -b "$branch" "$base"
    else
        g "$dir" merge-base --is-ancestor "$base" "$branch" \
            || die "$dir: branch $branch is not based on the pinned commit ${base:0:12}.
Rename it (git -C '$dir' branch -m $branch $branch-old) and re-run to recreate it from sources.lock."
        cur=$(g "$dir" symbolic-ref -q --short HEAD || true)
        if [ "$cur" != "$branch" ]; then
            info "switching $dir to branch $branch"
            g "$dir" checkout -q "$branch" 2>/dev/null || die "$dir: cannot check out $branch (is it checked out in another worktree? see git -C '$dir' worktree list)"
        fi
    fi

    mapfile -t have < <(branch_ids "$dir" "$base" "$branch")
    local i
    for i in "${!have[@]}"; do
        if [ "$i" -ge "${#want[@]}" ] || [ "${have[$i]}" != "${want[$i]}" ]; then
            die "$dir: branch $branch has commits that are not in $patches (commit $((i + 1)) of ${#have[@]} differs).
Your local work is untouched. Either export it into the patch series (git format-patch ${base:0:12}..$branch),
or rename the branch (git -C '$dir' branch -m $branch $branch-old) and re-run to rebuild it from the patches."
        fi
    done
    if [ "${#have[@]}" = "${#want[@]}" ]; then
        info "$name: branch $branch = base ${base:0:12} + ${#want[@]} patches (up to date)"
        return 0
    fi
    local all=("$patches"/*.patch)
    info "$name: applying patches $(( ${#have[@]} + 1 ))..${#want[@]} of ${#want[@]} on $branch"
    git_am "$dir" "${all[@]:${#have[@]}}"
}

# ---------------------------------------------------------------------------------------------------------------
# Install helpers
# ---------------------------------------------------------------------------------------------------------------
# Copy a file; if the target exists and differs, keep a timestamped backup of it.
install_cfg() {
    local src=$1 dst=$2
    mkdir -p "$(dirname "$dst")"
    if [ -f "$dst" ] && cmp -s "$src" "$dst"; then info "unchanged $dst"; return 0; fi
    if [ -f "$dst" ]; then
        cp -p "$dst" "$dst.bak-$(date +%Y%m%d-%H%M%S)"
        info "updated $dst (old version kept as .bak-*)"
    else
        info "installed $dst"
    fi
    install -m644 "$src" "$dst"
}

write_wrapper() {
    local dst=$1 body=$2 tmp
    tmp=$(mktemp)
    printf '%s\n' "$body" > "$tmp"
    if [ -f "$dst" ] && cmp -s "$tmp" "$dst"; then rm -f "$tmp"; return 0; fi
    install -D -m755 "$tmp" "$dst"; rm -f "$tmp"
    info "installed $dst"
}

cmake_require() {
    local cache=$1/CMakeCache.txt opt
    for opt in "${@:2}"; do
        grep -q "^$opt:BOOL=ON$" "$cache" || die "Monado was configured without $opt. Usually a dependency is
missing (Pimax driver: Corrosion, Ceres, OpenCV; compositor: Vulkan, glslangValidator). Run
'$0 --check', and see the configure output in $1/CMakeFiles/CMakeConfigureLog.yaml."
    done
}

# ---------------------------------------------------------------------------------------------------------------
# Steps
# ---------------------------------------------------------------------------------------------------------------
build_basalt() {
    step "Basalt (${PIN_BRANCH[basalt]} @ ${PIN_REV[basalt]:0:12} + patches) -> $PIMAX_PREFIX"
    info "updating submodules"
    g "$BASALT_SRC" submodule sync -q --recursive
    g "$BASALT_SRC" submodule update -q --init --recursive
    if [ ! -f "$BASALT_BUILD/CMakeCache.txt" ]; then
        cmake -S "$BASALT_SRC" -B "$BASALT_BUILD" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$PIMAX_PREFIX" \
            -DBASALT_BUILD_SHARED_LIBRARY_ONLY=ON
    fi
    cmake --build "$BASALT_BUILD" --parallel "$JOBS"
    cmake --install "$BASALT_BUILD" --prefix "$PIMAX_PREFIX"
}

build_monado() {
    step "Monado (${PIN_BRANCH[monado]} @ ${PIN_REV[monado]:0:12} + patches) -> $PIMAX_PREFIX"
    local inc
    inc=$(pimax_clang_include) || true
    [ -n "$inc" ] || die "clang resource include directory not found (needed by bindgen); install clang"
    export BINDGEN_EXTRA_CLANG_ARGS="-I$inc"
    # driver pins pimax-protocol in its Cargo.toml; keep sources.lock in sync
    local toml="$MONADO_SRC/src/xrt/drivers/pimax_p3c/Cargo.toml"
    if [ -n "${PIN_REV[pimax-protocol]:-}" ] && [ -f "$toml" ] && ! grep -q "${PIN_REV[pimax-protocol]}" "$toml"; then
        warn "pimax-protocol rev in $toml differs from sources.lock"
    fi
    cmake -S "$MONADO_SRC" -B "$MONADO_BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PIMAX_PREFIX"
    cmake_require "$MONADO_BUILD" XRT_BUILD_DRIVER_PIMAX_P3C XRT_FEATURE_SLAM XRT_HAVE_VULKAN XRT_FEATURE_STEAMVR_PLUGIN
    cmake --build "$MONADO_BUILD" --parallel "$JOBS"
    cmake --install "$MONADO_BUILD" --prefix "$PIMAX_PREFIX"
    # The SteamVR driver tree is installed to the configure-time prefix; make sure the prefix copy is current.
    local drv="$MONADO_BUILD/steamvr-monado/bin/linux64/driver_monado.so"
    [ -f "$drv" ] || die "missing $drv after the build"
    mkdir -p "$PIMAX_PREFIX/share/steamvr-monado"
    cp -a "$MONADO_BUILD/steamvr-monado/." "$PIMAX_PREFIX/share/steamvr-monado/"
    install -D -m755 "$drv" "$PIMAX_PREFIX/share/steamvr-monado/bin/linux64/driver_monado.so"
    info "SteamVR driver: $PIMAX_PREFIX/share/steamvr-monado"
}

build_tools() {
    step "OpenXR tools (tools/xrgaze) -> $PIMAX_PREFIX/bin"
    local inc="$MONADO_SRC/src/external/openxr_includes" src name libs out
    [ -f "$inc/openxr/openxr.h" ] || die "OpenXR headers not found in $inc"
    local tmp="$PIMAX_CACHE/tools-build"
    mkdir -p "$tmp" "$PIMAX_PREFIX/bin"
    for src in "$PIMAX_REPO"/tools/xrgaze/*.c; do
        name=$(basename "$src" .c)
        libs=(-l:libopenxr_loader.so.1 -lm)
        grep -q '#include <vulkan/vulkan.h>' "$src" && libs+=(-lvulkan)
        out="$tmp/$name"
        cc -O2 -include time.h -I"$inc" -o "$out" "$src" "${libs[@]}" || die "building $name failed"
        install -m755 "$out" "$PIMAX_PREFIX/bin/$name"
        info "$name"
    done
    # helpers used by Envision's Eye Tracking page
    install -D -m644 "$PIMAX_REPO/tools/xrgaze/fit_calib.py" "$PIMAX_PREFIX/share/pimax-linux/fit_calib.py"
    install -m755 "$PIMAX_REPO/scripts/pimax-et-calibrate" "$PIMAX_PREFIX/bin/pimax-et-calibrate"
    write_wrapper "$PIMAX_PREFIX/bin/pimax-gaze" "#!/bin/sh
# Live gaze viewer: grey dots = fixed references, white dot = where you look. Ctrl+C to quit.
exec \"$PIMAX_PREFIX/bin/xrcalib\" --live \"\${1:-600}\""
}

build_envision() {
    step "Envision (${PIN_REV[envision]:0:12} + patches) -> $ENVISION_PREFIX"
    # cargo's openxr crate links -lopenxr_loader: provide the unversioned link if only the runtime lib exists.
    local loader
    if ! grep -q '[[:space:]]libopenxr_loader.so ' <<<"$LDCACHE"; then
        loader=$(awk '$1=="libopenxr_loader.so.1"{print $NF; exit}' <<<"$LDCACHE")
        [ -n "$loader" ] || die "libopenxr_loader.so.1 not found"
        mkdir -p "$LINKDIR"
        ln -sfn "$loader" "$LINKDIR/libopenxr_loader.so"
        case ":${LIBRARY_PATH:-}:" in *":$LINKDIR:"*) ;; *) export LIBRARY_PATH="$LINKDIR${LIBRARY_PATH:+:$LIBRARY_PATH}" ;; esac
    fi
    [ -n "$DEVROOT" ] && info "using development files from $DEVROOT"
    if [ ! -f "$ENVISION_BUILD/build.ninja" ]; then
        meson setup "$ENVISION_BUILD" "$ENVISION_SRC" --prefix="$ENVISION_PREFIX"
    else
        local p
        p=$(meson introspect --buildoptions "$ENVISION_BUILD" 2>/dev/null \
            | python3 -I -c 'import json,sys; print(next(o["value"] for o in json.load(sys.stdin) if o["name"]=="prefix"))' || true)
        [ "$p" = "$ENVISION_PREFIX" ] || meson configure "$ENVISION_BUILD" --prefix="$ENVISION_PREFIX"
    fi
    meson compile -C "$ENVISION_BUILD"
    meson install -C "$ENVISION_BUILD" --quiet
    write_wrapper "$PIMAX_PREFIX/bin/envision-pimax" "#!/bin/sh
# Envision with the Pimax pages (pimax-linux branch \"$ENVISION_BRANCH\" of envision-src)
exec \"$ENVISION_PREFIX/bin/envision\" \"\$@\""
    # Menu entry (the one Envision installs lives under its own prefix, outside XDG_DATA_DIRS)
    local desk
    desk=$(ls "$ENVISION_PREFIX"/share/applications/*.desktop 2>/dev/null | head -1 || true)
    if [ -n "$desk" ]; then
        mkdir -p "$HOME/.local/share/applications"
        sed -e "s|^Exec=.*|Exec=$PIMAX_PREFIX/bin/envision-pimax %U|" -e "s|^Name=.*|Name=Envision (Pimax)|" \
            "$desk" > "$HOME/.local/share/applications/envision-pimax.desktop"
        info "menu entry: ~/.local/share/applications/envision-pimax.desktop"
    fi
}

install_configs() {
    step "Configs -> $CONFIG_HOME"
    local f
    for f in "$CONFIGS_SRC"/displays/*.json; do install_cfg "$f" "$CONFIG_HOME/monado/pimax_p3c/displays/$(basename "$f")"; done
    for f in "$CONFIGS_SRC"/controllers/*.json; do install_cfg "$f" "$CONFIG_HOME/monado/pimax_p3c/controllers/$(basename "$f")"; done

    if [ -f "$PIMAX_REPO/config/51-pimax-crystal-audio.conf" ]; then
        install_cfg "$PIMAX_REPO/config/51-pimax-crystal-audio.conf" "$CONFIG_HOME/wireplumber/wireplumber.conf.d/51-pimax-crystal-audio.conf"
    else
        warn "config/51-pimax-crystal-audio.conf not found, skipping the WirePlumber rule"
    fi

    local libdir tmp
    libdir=$(pimax_libdir "$PIMAX_PREFIX" libbasalt.so) || die "libbasalt.so not found under $PIMAX_PREFIX"
    tmp=$(mktemp)
    sed "s|^VIT_SYSTEM_LIBRARY_PATH=.*|VIT_SYSTEM_LIBRARY_PATH=$libdir/libbasalt.so|" "$PIMAX_REPO/config/90-vr-pimax.conf" > "$tmp"
    install_cfg "$tmp" "$CONFIG_HOME/environment.d/90-vr-pimax.conf"
    rm -f "$tmp"
}

install_udev() {
    step "udev rules -> /etc/udev/rules.d (sudo)"
    local r rules=("$PIMAX_REPO"/config/99-*.rules)
    [ -e "${rules[0]}" ] || { warn "no config/99-*.rules found"; return 0; }
    for r in "${rules[@]}"; do
        info "$(basename "$r")"
        sudo install -m644 "$r" "/etc/udev/rules.d/$(basename "$r")"
    done
    sudo udevadm control --reload-rules
    sudo udevadm trigger
}

install_system() {
    step "Monado + Basalt -> /usr (sudo)"
    # Envision's default profile runs /usr/bin/monado-service. Installing the build trees again with prefix /usr
    # brings the service, libraries, symlinks and the OpenXR manifest (its library path is relative).
    local f owner
    for f in /usr/bin/monado-service /usr/share/openxr/1/openxr_monado.json; do
        [ -e "$f" ] || continue
        owner=$( (rpm -qf "$f" || dpkg -S "$f" || pacman -Qoq "$f") 2>/dev/null | head -1 || true)
        case $owner in ""|*"not owned"*|*"gehört zu keinem"*|*"no path found"*) ;;
            *) warn "$f belongs to the distro package '$owner' and will be overwritten" ;; esac
    done
    local b
    for b in "$BASALT_BUILD" "$MONADO_BUILD"; do
        sudo cmake --install "$b" --prefix /usr
        # cmake writes install_manifest.txt as root; give it back so later user installs can rewrite it
        [ -e "$b/install_manifest.txt" ] && sudo chown "$(id -u):$(id -g)" "$b/install_manifest.txt"
    done
    sudo ldconfig
}

# ---------------------------------------------------------------------------------------------------------------
read_pins
setup_build_env
step "Checking build dependencies"
check_deps
if [ "$CHECK_ONLY" = 1 ]; then report_deps; exit $?; fi
report_deps || die "install the missing dependencies first (nothing was changed)"

step "Sources -> $PIMAX_SRC"
checkout_pinned pimax-configs "$CONFIGS_SRC"
checkout_patched basalt "$BASALT_SRC" "$BASALT_BRANCH" "$PIMAX_REPO/patches/basalt"
checkout_patched monado "$MONADO_SRC" "$MONADO_BRANCH" "$PIMAX_REPO/patches/monado"
[ "$ENVISION" = 1 ] && checkout_patched envision "$ENVISION_SRC" "$ENVISION_BRANCH" "$PIMAX_REPO/patches/envision"

mkdir -p "$PIMAX_CACHE" "$PIMAX_PREFIX"
build_basalt
build_monado
build_tools
[ "$ENVISION" = 1 ] && build_envision
install_configs
[ "$UDEV" = 1 ] && install_udev
[ "$SYSTEM" = 1 ] && install_system

step "Done"
info "installed into $PIMAX_PREFIX (bin: monado-service, xr* tools, pimax-gaze, pimax-et-calibrate$([ "$ENVISION" = 1 ] && echo ", envision-pimax"))"
[ "$PIMAX_PREFIX" = "$HOME/.local" ] || warn "Envision's Pimax pages and pimax-et-calibrate look for their tools in ~/.local; with PIMAX_PREFIX=$PIMAX_PREFIX they will not find them"
case ":$PATH:" in *":$PIMAX_PREFIX/bin:"*) ;; *) warn "$PIMAX_PREFIX/bin is not in PATH" ;; esac
cat <<EOF
Next:
  - log out and in again (environment.d), and restart WirePlumber: systemctl --user restart wireplumber
EOF
[ "$UDEV" = 1 ] || echo "  - udev rules for the headset were not installed (re-run with --udev, needs sudo)"
[ "$SYSTEM" = 1 ] || echo "  - Envision's default profile runs /usr/bin/monado-service: re-run with --system (sudo) after every build"
echo "  - NVIDIA: apply patches/nvidia (see README), WayVR AppImage at $PIMAX_WAYVR"
[ "$ENVISION" = 1 ] && echo "  - start: envision-pimax (Plugins -> WayVR, then Start)"
exit 0

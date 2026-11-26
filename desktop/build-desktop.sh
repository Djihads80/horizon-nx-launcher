#!/usr/bin/env bash
# Builds the launcher for a PC, to look at its UI without a console.
#
#   ./desktop/build-desktop.sh            build it (needs meson, ninja, glfw3, glm, curl headers)
#   ./desktop/build-desktop.sh run        build it, then run it under a virtual screen and keep a
#                                         screenshot in desktop/scratch/
#
# The Switch build (build.sh) and this one compile the same source/*.cpp. The only difference is
# desktop/compat/switch.h, which stubs the libnx calls, and the `romfs:` path: it is a symlink to the
# generated romfs, so the pictures load exactly as they do on the console.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
BOREALIS="${BOREALIS_PATH:-$ROOT/.borealis}"
OUT="$HERE/scratch"
SCRATCH="$OUT/card"

build_borealis() {
    if [ ! -d "$OUT/borealis" ]; then
        meson setup "$OUT/borealis" "$BOREALIS" -Dbuildtype=debug >/dev/null
    fi
    ninja -C "$OUT/borealis" >/dev/null
}

# The card layout the app expects: sdmc: is a folder with that name, which is a legal directory name
# on Linux, so path() ("sdmc:/switch/horizon/...") needs no change.
card() {
    mkdir -p "$SCRATCH/sdmc:/switch/horizon/instances/minecraft-26-3"

    # Where the app looks for its pictures ("romfs:/...") and where the library looks for its fonts
    # ("resources/..."): plain symlinks, made once. With a trailing slash ln follows the link and
    # writes a link back into the real romfs, which then packs itself into the NRO forever.
    [ -e "$SCRATCH/romfs:" ] || ln -s "$ROOT/romfs" "$SCRATCH/romfs:"
    [ -e "$SCRATCH/resources" ] || ln -s "$ROOT/romfs" "$SCRATCH/resources"
    if [ ! -f "$SCRATCH/sdmc:/switch/horizon/launcher.json" ]; then
        cat > "$SCRATCH/sdmc:/switch/horizon/launcher.json" <<'EOF'
{
  "instances": [
    { "id": "minecraft-26-3", "name": "Minecraft 26.3", "icon": "grass", "args": "", "played": 0 },
    { "id": "create", "name": "Create a World", "icon": "diamond", "args": "", "played": 7200 },
    { "id": "test", "name": "Test Instance", "icon": "star", "args": "", "played": 60 }
  ],
  "selected": "minecraft-26-3",
  "accounts": [],
  "theme": 0,
  "globalArgs": ""
}
EOF
    fi
}

build_app() {
    mkdir -p "$OUT/obj"
    local sources=("$ROOT"/source/*.cpp "$ROOT"/third_party/qrcodegen.cpp)
    local objects=()
    for source in "${sources[@]}"; do
        local object="$OUT/obj/$(basename "${source%.cpp}").o"
        objects+=("$object")
        if [ ! -f "$object" ] || [ "$source" -nt "$object" ]; then
            g++ -std=c++17 -g -O1 -c "$source" -o "$object" \
                -I "$HERE/compat" -I "$ROOT/source" -I "$ROOT/third_party" \
                -I "$BOREALIS/library/include" -I "$BOREALIS/library/include/borealis/extern" \
                -I "$BOREALIS/library/lib/extern/fmt/include" \
                -DBOREALIS_RESOURCES="\"romfs:/\"" -DHORIZON_DESKTOP
        fi
    done

    # Borealis' own objects, minus the example's main().
    mapfile -t borealis_objects < <(find "${BR_BLD:-$OUT/borealis}" -name '*.o' ! -name '*example*' | sort)
    g++ "${objects[@]}" "${borealis_objects[@]}" -o "$OUT/horizon-launcher" \
        $(pkg-config --libs glfw3) -lcurl -lpthread -ldl -lm
    echo "==> $OUT/horizon-launcher"
}

run() {
    cd "$SCRATCH"
    rm -f ../shot*.png
    if command -v xvfb-run >/dev/null; then
        xvfb-run -a -s "-screen 0 1280x720x24" "$OUT/horizon-launcher" &
        local pid=$!
        sleep 6
        import -window root ../shot.png 2>/dev/null || xwd -root -silent | convert xwd:- ../shot.png 2>/dev/null || true
        kill $pid 2>/dev/null || true
        wait $pid 2>/dev/null || true
        echo "==> $OUT/shot.png"
    else
        "$OUT/horizon-launcher"
    fi
}

case "${1:-build}" in
    build) build_borealis; card; build_app ;;
    run) build_borealis; card; build_app; run ;;
    *) echo "usage: $0 [build|run]" >&2; exit 2 ;;
esac

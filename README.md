# Horizon Launcher for the Nintendo Switch

A homebrew launcher for [Horizon](https://github.com/KoHaku743/horizon-nx), the Minecraft: Java Edition
port for the Switch, that runs on the console itself. No PC, no companion app, no hekate: a normal
`.nro` started from Sphaira or the homebrew menu, like every other homebrew app.

It does the four things you need on a console:

- **Play** — pick an instance, then Play. The launcher hands over to `horizon.nro` with that
  instance's game folder and arguments (`envSetNextLoad`, the same call hbmenu uses), so the game
  starts as if you had launched it directly.
- **Instances** — one game folder, arguments and picture each: create, rename, delete, choose the
  picture, and see how long each has been played.
- **Mods** — browse Modrinth for mods, resource packs, shader packs and data packs, and install one
  into the selected instance, with its SHA-512 checked against the download.
- **Settings** — Microsoft accounts to switch between, dark or light appearance, global arguments,
  and the sign-in's client ID.

Sign-in is Microsoft's device-code flow, the one the game itself uses: the page shows a QR code and
the code in large type, you approve it on a phone or PC, and the account is written into
`switch/horizon/account.json` in the shape the game reads. **Minus** opens it from any screen.

| | |
|---|---|
| Home screen | `Play`, `Instances`, `Mods`, `Settings` tabs on the left, the signed-in player and account count at the top right |
| Controls | Stick/D-pad to move, **A** to choose, **B** to go back, **+** to exit, **−** to sign in |
| Where it lives | `sdmc:/switch/horizon/launcher.nro`, settings in `sdmc:/switch/horizon/launcher.json` |

It must run as an **application**, not an applet: hold **R** while starting any game to open the
homebrew menu as an application, then choose the launcher. Started as an applet it says so and exits
instead of running out of memory halfway.

## Installing

Copy these to the SD card:

```
switch/horizon/launcher.nro      this launcher
switch/horizon/horizon.nro       the game it starts
```

The launcher's own settings file (`launcher.json`) appears the first time it saves something.

## The Microsoft sign-in's client ID

Signing in needs an Azure app registration, and Minecraft only accepts ones Mojang has approved:

- **A third-party app** — what the game's own build uses: an Azure "public client" with the device-code
  flow enabled, approved for the Minecraft API. Its ID is the one that signs in.
- **A first-party Microsoft app** — a Microsoft-owned ID: the sign-in page refuses to let anyone
  consent to it ("the application is a first party application … should obtain pre-authorization for
  the resource server"), and Minecraft answers `401 UNAUTHORIZED` to both `launcher/login` and
  `authentication/login_with_xbox`. Signing in cannot work with one; the launcher says so instead of
  failing vaguely.

The launcher reads the ID from, in order: Settings → Sign-in → **Microsoft client ID**, then
`HORIZON_MSA_CLIENT_ID=` in `switch/horizon/env.txt`, then the build:

```sh
HORIZON_MSA_CLIENT_ID=<id> ./build.sh
```

The build writes it to `build/build_config.hpp`, which is gitignored — this repository carries no ID.
Use the same ID the game uses, so both sign in through one app.

## Building

Needs Docker (or Podman) on Linux, and nothing else installed on the host:

```sh
./build.sh            # the toolchain image if it is missing, then horizon-launcher.nro
./build.sh probe      # the OpenGL probe, if the launcher's UI does not come up
./build.sh clean
```

`build.sh` clones [Borealis](https://github.com/natinusala/borealis)' `legacy` branch into
`./.borealis` at a pinned revision and applies the launcher's two small patches to it, builds the
toolchain image (devkitPro + GLFW/GLES, Mesa, curl, zlib) once, and then the app. The pictures in
`romfs/` are drawn by `make-assets.py`: no Minecraft art is shipped.

## Layout

```
source/          the app: main, pages (Play/Instances/Mods/Settings/Sign-in), auth, registry, ui
third_party/     nlohmann/json and Nayuki's QR generator, both MIT
patches/         the two changes made to Borealis
tests/           the logic tests, built and run on a PC
gl-probe/        a four-line app that reports which GL contexts the console will give
make-assets.py   draws the launcher's pictures and instance icons
```

`tests/logic_test.cpp` covers what can be checked without a console — instance bookkeeping, the launch
command line, account files, playtime formatting:

```sh
g++ -std=c++17 -I third_party -I source tests/logic_test.cpp \
    source/config.cpp source/util.cpp source/sha512.cpp -o /tmp/logic_test && /tmp/logic_test
```

## Credits

- [Horizon](https://github.com/KoHaku743/horizon-nx) — the port this launches; its sign-in and
  instance layout are the reference this follows.
- [Borealis](https://github.com/natinusala/borealis) — the UI library (Apache-2.0), patched here.
- [nlohmann/json](https://github.com/nlohmann/json) and [Nayuki's QR generator](https://github.com/nayuki/QR-Code-generator) — MIT.
- Not affiliated with Mojang or Microsoft.

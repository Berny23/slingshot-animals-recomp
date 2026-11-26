# Building and development

For players, the [Slingshot Builder](../README.md#install) does all of this.

## Build

Needs CMake 3.28, Ninja or Make and a C++20 compiler. On Linux also capstone, SDL3, FreeType, HarfBuzz, liblzma and zlib; elsewhere these are downloaded and built (`-DSLINGSHOT_SYSTEM_DEPS=OFF` does the same on Linux).

```
cmake -S . -B build -DGAME_APKS="/path/to/angrybirds.apk;/path/to/angrybirdsrio.apk"
cmake --build build
```

Each APK is matched to its game by the library and extracted to `build/games/<id>/`; every game gets its own executable `build/bin/slingshot-<id>`. The build checks each library's SHA-256. Recompiling a game needs up to 1.5 GB of memory, so games are recompiled one at a time. Build options:

- `-DSLINGSHOT_DEFAULT_MODS=default|all|none|<list>`: mods enabled by default, e.g. `"hide-dead-services;offline-shop"`; `default` is all but those marked *(off)*
- `-DSLINGSHOT_MODS=OFF`: build without mods
- `-DGAME_EXTRA_starwars2=<zip or folder>`: the Star Wars II assets (see [README](../README.md#star-wars-ii-assets))

Cross builds use the recompiler built for this machine (`-DSLINGSHOT_HOST_TOOLS=build/host-tools`):

- Windows: `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/llvm-mingw.cmake -DLLVM_MINGW=<llvm-mingw>`
- Nintendo Switch: `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/switch.cmake -DDEVKITPRO=<devkitpro>` builds `bin/slingshot-<id>.nro`; needs devkitPro's `devkitA64`, `libnx`, `switch-tools` and `switch-sdl2`
- Android: `-DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DSLINGSHOT_ANDROID_SDK=<sdk> -DSLINGSHOT_JDK=<jdk>` builds `bin/slingshot-<id>.apk`, signed with `-DSLINGSHOT_ANDROID_KEYSTORE` (created if missing; updates need the same key)

## Builder app

A separate project: `cmake -S tools/builder -B build-builder && cmake --build build-builder` builds `build-builder/slingshot-builder` (Qt 6.5 or newer). It builds in KDE's Breeze style and uses it everywhere; `-DSLINGSHOT_BUILDER_BREEZE=OFF` keeps Qt's platform styles.

## Run

```
build/bin/slingshot-<id> [options] [game-dir]
```

| Option | Description |
| --- | --- |
| `--help` | Show the options |
| `--list-mods` | List the mods |
| `--mod <name>` | Enable only the given mods (repeatable), like `SLINGSHOT_MODS=a,b` |
| `--enable-mod <name>` | Enable a mod in addition to the others |
| `--disable-mod <name>` | Disable a mod |
| `--no-mods` | Original game without mods |
| `--mod-option <mod>.<key>=<value>` | A setting for a mod (repeatable), see the [mod list](MODS.md) |
| `--locale <ll_CC>` | Game language, e.g. `de_DE` |
| `--data <dir>` | Keep the saves in `<dir>` instead of the default folder |
| `--dump-scripts <dir>` | Write the game's scripts and configs out decrypted |

Saves from where earlier versions kept them, `~/.local/share/slingshot-animals-recomp/<id>/` (or `$XDG_DATA_HOME`) and `%APPDATA%\slingshot-animals-recomp\<id>\`, are copied to `saves/` on the first start; where the game's folder cannot be written, they stay in those places. On Android, saves of earlier versions are moved to the new folder on the first start.

## Modding

The game logic is Lua 5.1 bytecode (4-byte numbers), the configs are JSON. In the APK both are encrypted and compressed; the games read plain scripts too:

1. `build/bin/slingshot-<id> --dump-scripts ~/ab-scripts` writes the scripts (`data/**/*.lua`) and JSON files in the layout of `assets/`, decrypted by the game's own code. Classic also writes `config.json` and `data/config/*.zip`. A script that is a 7z archive is written as `<name>.7z` where the game's loader for it is not hooked.
2. Replace a script in `build/games/<id>/assets/` with an edited one, as Lua source or bytecode. Classic and Seasons take plain JSON files and configs too.

Sprite sheet descriptions (`*.sheet.json`, `*.compo.json`) are dumped for reference but cannot be replaced yet. The built-in mods patch scripts in memory, see [ARCHITECTURE.md](ARCHITECTURE.md).

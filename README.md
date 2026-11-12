# slingshot-animals-recomp

Unofficial static recompilation of *Angry Birds Classic* 8.0.3 for Android. The game's native ARM library is translated to C++ and runs natively on x86-64 Linux, without an emulator.

## Features

- Runs the original game logic, graphics, sound and text rendering natively
- Plays offline: Rovio's online services, ads and in-app purchases are stubbed
- Mouse or touch screen as on Android, Esc is the Back key
- Language follows the system locale or `--locale`
- Optional mods (all enabled by default):

| Mod | Description |
| --- | --- |
| `wheel-zoom` | Mouse wheel zooms in levels and scrolls lists in menus |
| `keyboard` | Space + W A S D or arrows aims, release shoots (Shift: fine), Space in flight uses the bird's ability. Arrows move the camera, + / - zoom, Z switches the view, R restarts, P pauses, Enter confirms |
| `gamepad-pointer` | Left stick moves a cursor (right stick: fine), A touches, B / Start is Back, triggers zoom |
| `skip-level-end` | Tap to skip the wait at the end of a level |
| `fullscreen` | F11 toggles fullscreen |
| `hide-dead-services` | Hides what only leads to discontinued online services: news, more games, Rovio Account, Daily Challenge, ad videos, the world leaderboard and server-only trophies |
| `offline-league` | Mighty League without servers: progress is kept, and after the Feather League come rounds in the Glass to Master leagues with their levels, opponents, eggs, rewards and trophies. Profile page and emblem editor work |
| `offline-shop` | Shop without real money: no gem packs, the Mighty Eagle costs 1500 coins, levels pay coins and gems |
| `game-statistics` | Statistics page with the progress to 100 %, the game's counters and its Google Play achievements, with a banner when one is unlocked |

## Supported version

- Angry Birds Classic 8.0.3 (build 8031), `com.rovio.angrybirds`, APK with the `armeabi-v7a` library

The build checks the library's SHA-256.

## Build

Needs CMake 3.24, a C++20 compiler, Python 3, capstone, SDL3, FreeType, HarfBuzz, fontconfig and liblzma.

```
cmake -S . -B build -DGAME_APK=/path/to/angrybirds.apk
cmake --build build
```

The APK's library and assets are extracted to `build/game/`. Build options:

- `-DSLINGSHOT_DEFAULT_MODS=all|none|<list>`: mods enabled by default, e.g. `"hide-dead-services;offline-shop"`
- `-DSLINGSHOT_MODS=OFF`: build without mods

## Run

```
build/platform/sdl/slingshot [options] [game-dir]
```

| Option | Description |
| --- | --- |
| `--list-mods` | List the mods |
| `--mod <name>` | Enable only the given mods (repeatable), like `SLINGSHOT_MODS=a,b` |
| `--no-mod <name>` | Disable a mod |
| `--no-mods` | Original game without mods |
| `--locale <ll_CC>` | Game language, e.g. `de_DE` |
| `--dump-scripts <dir>` | Write the game's scripts and configs out decrypted |

Saves are in `~/.local/share/slingshot-animals-recomp/` (or `$XDG_DATA_HOME`). They are encrypted by the game; copy the folder while the game is not running to back them up.

## Modding

The game logic is Lua 5.1 bytecode (4-byte numbers), the configs are JSON. In the APK both are encrypted and compressed, but plain files are read too:

1. `build/platform/sdl/slingshot --dump-scripts ~/ab-scripts` writes `data/scripts/**/*.lua`, `config.json` and `data/config/*.zip` in the layout of `assets/`.
2. Replace a file in `build/game/assets/` with an edited one. Scripts can be Lua source.

Sprite sheet descriptions (`*.sheet.json`, `*.compo.json`) are dumped for reference but cannot be replaced yet. The built-in mods patch scripts in memory, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Note

Only tested on x86-64 Linux.

This project does not include any original game files. You need your own copy of the APK.

## License

[GPL-3.0-or-later](LICENSE)

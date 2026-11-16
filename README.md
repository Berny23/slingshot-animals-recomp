# slingshot-animals-recomp

Unofficial static recompilation of Rovio's Android *Angry Birds* games. Each game's native ARM library is translated to C++ and runs natively on x86-64 Linux, without an emulator.

## Features

- Runs the original game logic, graphics, sound and text rendering natively
- Plays offline: Rovio's online services, ads and in-app purchases are stubbed
- Mouse or touch screen as on Android, Esc is the Back key
- Language follows the system locale or `--locale`
- Optional mods (all enabled by default):

| Mod | Games | Description |
| --- | --- | --- |
| `mouse-wheel` | all | Mouse wheel zooms in levels and scrolls menus |
| `keyboard` | all | Play with the keyboard: Space + W A S D or arrows aims and shoots, R restarts, P pauses |
| `gamepad-pointer` | all | Play with a gamepad: the left stick moves a cursor, A taps, B is Back |
| `tap-to-skip` | all | Tap to skip the wait at the end of a level |
| `fullscreen` | all | F11 toggles fullscreen |
| `hide-dead-services` | all | Hides buttons and messages of discontinued online services |
| `offline-league` | Classic | Mighty League without servers |
| `offline-tournament` | Seasons | Pig Challenge without servers: weeks open one after another, all weeks replayable after the last |
| `offline-shop` | all | Everything sold for real money can be earned by playing |
| `composite-audio` | Star Wars, Star Wars II | Plays the menu music and power-up sounds again |
| `offline-assets` | Star Wars II | Starts without the download server (see below) |
| `audio-quality` | Stella | Better sound than on Android |
| `seasonal-menu` | Seasons | The main menu shows a random episode of the current season (Halloween, Christmas, ...). `--mod-option seasonal-menu.season=<name>` picks one |
| `offline-clock` | Star Wars II | Timed features use the device's clock |
| `missing-translations` | Rio | Translates texts the game shows only in English |
| `game-statistics` | all | Statistics page with achievements and progress to 100 % |
| `samsung-level` | Space, Star Wars II | Content once given only to Samsung devices is open everywhere |

## Supported versions

APKs with the `armeabi-v7a` library:

| Id | Game | Version | Package |
| --- | --- | --- | --- |
| `classic` | Angry Birds Classic | 8.0.3 (build 8031) | `com.rovio.angrybirds` |
| `seasons` | Angry Birds Seasons | 6.6.2 (build 6620) | `com.rovio.angrybirdsseasons` |
| `rio` | Angry Birds Rio | 2.6.13 (build 2676) | `com.rovio.angrybirdsrio` |
| `space` | Angry Birds Space HD | 2.2.14 (build 221400) | `com.rovio.angrybirdsspaceHD` |
| `starwars` | Angry Birds Star Wars HD | 1.5.13 (build 151300) | `com.rovio.angrybirdsstarwarshd.premium.iap` |
| `starwars2` | Angry Birds Star Wars II | 1.9.25 (build 192500) | `com.rovio.angrybirdsstarwarsii.ads` |
| `stella` | Angry Birds Stella | 1.1.5 (build 1152) | `com.rovio.angrybirdsstella` |

The build checks each library's SHA-256.

Star Wars II downloads its sounds and theme images from a server that is gone; without them it plays silence and draws simplified backgrounds. For the originals, copy `files/assets_service/` from a device installation (`/data/data/com.rovio.angrybirdsstarwarsii.ads/`) to:

```
~/.local/share/slingshot-animals-recomp/starwars2/internal/files/assets_service/
├── 16000hz-16bit-mono/    music/ and sfx/ (*.mp3)
└── images/                android/ (THEME_*.pvr, THEME_*.dat)
```

## Build

Needs CMake 3.24, a C++20 compiler, Python 3, capstone, SDL3, FreeType, HarfBuzz, fontconfig, liblzma and zlib.

```
cmake -S . -B build -DGAME_APKS="/path/to/angrybirds.apk;/path/to/angrybirdsrio.apk"
cmake --build build
```

Each APK is matched to its game by the library and extracted to `build/games/<id>/`; every game gets its own executable `build/bin/slingshot-<id>`. Recompiling a game needs up to 1.5 GB of memory, so games are recompiled one at a time. Build options:

- `-DSLINGSHOT_DEFAULT_MODS=all|none|<list>`: mods enabled by default, e.g. `"hide-dead-services;offline-shop"`
- `-DSLINGSHOT_MODS=OFF`: build without mods

## Run

```
build/bin/slingshot-<id> [options] [game-dir]
```

| Option | Description |
| --- | --- |
| `--list-mods` | List the mods |
| `--mod <name>` | Enable only the given mods (repeatable), like `SLINGSHOT_MODS=a,b` |
| `--no-mod <name>` | Disable a mod |
| `--no-mods` | Original game without mods |
| `--mod-option <mod>.<key>=<value>` | A setting for a mod (repeatable), see the mod table |
| `--locale <ll_CC>` | Game language, e.g. `de_DE` |
| `--dump-scripts <dir>` | Write the game's scripts and configs out decrypted |

Saves are in `~/.local/share/slingshot-animals-recomp/<id>/` (or `$XDG_DATA_HOME`). They are encrypted by the game; copy the folder while the game is not running to back them up.

## Modding

The game logic is Lua 5.1 bytecode (4-byte numbers), the configs are JSON. In the APK both are encrypted and compressed; the games read plain scripts too:

1. `build/bin/slingshot-<id> --dump-scripts ~/ab-scripts` writes the scripts (`data/**/*.lua`) and JSON files in the layout of `assets/`, decrypted by the game's own code. Classic also writes `config.json` and `data/config/*.zip`. A script that is a 7z archive is written as `<name>.7z` where the game's loader for it is not hooked.
2. Replace a script in `build/games/<id>/assets/` with an edited one, as Lua source or bytecode. Classic and Seasons take plain JSON files and configs too.

Sprite sheet descriptions (`*.sheet.json`, `*.compo.json`) are dumped for reference but cannot be replaced yet. The built-in mods patch scripts in memory, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Note

Only tested on x86-64 Linux.

This project does not include any original game files. You need your own copies of the APKs.

## License

[GPL-3.0-or-later](LICENSE)

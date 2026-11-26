<p align="center"><img src="tools/builder/resources/builder.svg" alt="" width="128"></p>

<h1 align="center">slingshot-animals-recomp</h1>

<p align="center"><a href="https://recomplauncher.com/game/slingshot-animals-recomp"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a></p>

Unofficial static recompilation of Rovio's old Android *Angry Birds* slingshot games, for Linux, Windows, macOS, 64-bit Android and the Nintendo Switch (homebrew).

> [!IMPORTANT]
> **No game files are included or linked here.** You need your own legally obtained copy of each game.
>
> The game is built locally on your device, for your personal use only.

## Features

- All 7 games run natively, fast and are fully playable
- Full level builder/editor with QR code sharing for Classic
- Online content is restored, shops use in-game money, levels provide more currency
- Achievements, statistics and a new 100 % completion tracker
- Controller, keyboard, mouse and touch input
- Supports each game's original languages
- Many mods included, [see the list](docs/MODS.md)

## Install

1. Download **Slingshot Builder** for your system from the [releases](https://github.com/Berny23/slingshot-animals-recomp/releases/latest) (Linux AppImage, Windows exe, macOS app).

2. Open the builder:
    - **Linux**: Just run the AppImage. It can be updated with tools like [Gear Lever](https://flathub.org/apps/it.mijorus.gearlever) or AppImageUpdate.
    - **Windows**: As the builder is not signed, SmartScreen may warn you, which is normal. Just click *More info* → *Run anyway*.
    - **MacOS**: Open the builder with right-click → *Open* the first time. It needs Apple's Command Line Tools and offers to install them.

3. Drop the APK of a [supported version](#supported-versions) onto the window, then build for your favorite platform:
    - **Linux/Windows/macOS**: Press *Build & Install*. Each game then appears in your system as a separate app.
    - **Android**: Select *Build Android APK only...*, choose a location and wait for building to finish. Then copy the generated APK file onto your phone and install it. Advanced users can alternatively install via ADB directly on a connected phone.
    - **Nintendo Switch (homebrew)**: Select *Build for Nintendo Switch (homebrew)...* wait for building to finish. Then copy the `.nro` file into `switch/` on your SD card and start the game from the Homebrew Menu.

Saves are located next to the game files, on Android in the app's `media` directory.

Uninstall on Linux: Delete its folder in `~/.local/lib/slingshot-animals-recomp/` (saves are there too) and `~/.local/share/applications/slingshot-<id>.desktop`.

## Supported versions

Only APKs with the `armeabi-v7a` library work:

| Id | Game | Version | Package |
| --- | --- | --- | --- |
| `classic` | Angry Birds Classic | 8.0.3 | `com.rovio.angrybirds` |
| `seasons` | Angry Birds Seasons | 6.6.2 | `com.rovio.angrybirdsseasons` |
| `rio` | Angry Birds Rio | 2.6.13 | `com.rovio.angrybirdsrio` |
| `space` | Angry Birds Space HD | 2.2.14 | `com.rovio.angrybirdsspaceHD` |
| `starwars` | Angry Birds Star Wars HD | 1.5.13 | `com.rovio.angrybirdsstarwarshd.premium.iap` |
| `starwars2` | Angry Birds Star Wars II | 1.9.25 | `com.rovio.angrybirdsstarwarsii.ads` |
| `stella` | Angry Birds Stella | 1.1.5 | `com.rovio.angrybirdsstella` |

### Star Wars II assets

Star Wars II downloaded its music and theme images from a server that is gone. Without them it has no music and plain backgrounds. If you have the game installed on an Android device, take `files/assets_service/` from `/data/data/com.rovio.angrybirdsstarwarsii.ads/` and give it to the builder (as a ZIP file or folder) when it asks. Or copy it into the game's [save folder](#saves) as `internal/files/assets_service/`.

## Saves

| System | Folder |
| --- | --- |
| Linux, Windows | `saves/` next to the game |
| macOS | `~/Library/Application Support/slingshot-animals-recomp/<id>/` |
| Android | `Android/media/<package>.recomp/<id>/` (uninstalling deletes it) |
| Switch | `sdmc:/switch/slingshot-animals-recomp/<id>/` |

To back up, copy the folder while the game is not running.

## Building and contributing

See [docs/BUILDING.md](docs/BUILDING.md) for building from source, command line options and editing the game's scripts. **Feel free to create pull requests for new mods and fixes.** Make sure to test your changes and produce clean code if you use LLMs. :)

## Copyright

This project does not include any original game files. You need your own copies of the APKs. It is not affiliated with or endorsed by Rovio; *Angry Birds* is a trademark of Rovio Entertainment.

## License

This repository is licensed under [GPL-3.0-or-later](LICENSE).

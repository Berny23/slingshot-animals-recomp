# Architecture

The native library of `com.rovio.angrybirds` 8.0.3 (build 8031) is recompiled to C++ at build time, and a host runtime replaces Android and Java. Nothing derived from the APK is part of the repository.

## 1. The APK

| Item | Content |
|---|---|
| Native code | `lib/armeabi-v7a/libAngryBirdsClassic.so` (11 MB, stripped). An x86 build exists too |
| ARM build | ARMv7-A, Thumb-2 and ARM, VFPv3 and NEON, softfp ABI |
| Function info | `.ARM.exidx`: 18 263 entries with function starts and unwind data |
| C++ exceptions | Used throughout; the unwinder is linked into the library |
| Relocations | 16 727 `R_ARM_RELATIVE`, 344 `JUMP_SLOT`, 23 `GLOB_DAT`, 11 `ABS32` |
| Imports | 352: bionic libc/libm/pthread, GLES 2.0, `AAssetManager`, `liblog`, `__system_property_get` |
| Embedded libraries | Lua 5.1, libpng, zlib, libcurl, OpenSSL, protobuf, physics |
| Java → native | `NativeApplication.native{Init,Config,Resize,Update,Render,Pause,Resume,Deinit}`, `MyInputHandler.native{Input,KeyInput,InputAxis}`, `AudioOutput.nativeMixData` |
| Native → Java | `com/rovio/fusion/*` (fonts, audio, device info, text input, …) and `com/rovio/rcs/*` (online services) |
| Assets | Encrypted Lua scripts, Ogg audio, PNG/PVR textures, shaders, levels, texts |

`libjs.so` and `libadcolony.so` only serve the Java ad and web view layer and are not used.

The ARMv7 build is recompiled rather than the x86 one: fixed-width instructions, flags only on `S` instructions, softfp passes all arguments in r0–r3 and on the stack, no x87, and `.ARM.exidx` gives function starts and the EHABI unwind data.

## 2. Pipeline

```
APK ──► tools/scripts/extract_apk.py ──► build/game/{lib,assets}
                         │
                         ▼
          tools/recompiler (abrecomp, capstone)
   ELF + exidx → function discovery → CFG, jump tables → C++
                         │
                         ▼
          build/generated/*.cpp  +  runtime/  ──►  slingshot (platform/sdl)
```

## 3. Runtime

### 3.1 Guest memory
The guest stays 32-bit: a 4 GiB host reservation, guest address `a` is host `g_mem + a`. The library is loaded at its link address (base 0) from the extracted `.so`, and its relocations are applied at start; the generated code contains no game data. Page 0 is unmapped. The runtime's static data, the heap (slab allocator for small blocks, pages for large ones), guest stacks, the fake JNIEnv and import thunks live in fixed ranges (`runtime/core/include/recomp/memory.hpp`).

### 3.2 Generated code
- Each guest function is `void fn_<addr>(CPUContext*)`. Registers r0–r15, NZCV, Q, GE and `d0–d31` are in the context.
- `BL`/`BLX` to known functions are direct calls. Indirect calls and branches go through `dispatch(ctx, addr)`, a generated address table.
- Branches inside a function are `goto`s. Jump tables (`TBB`/`TBH`, `ADD PC`, `LDR PC`) are resolved statically. Tail calls are a call plus return.
- Literal pools in `.text` are recognized and not decoded as code.
- `abrecomp emit --wrap <addr>=<hook>` routes a function's entry to the runtime hook `hook:<hook>`. The original stays callable as `fn_<addr>_original` (`recomp::find_original`). `game/CMakeLists.txt` lists the wrapped functions.

### 3.3 Imports (HLE)
Each import resolves to a thunk address that `dispatch` routes to a host implementation (`runtime/hle/`):
- libc: strings and math call the host. Files, directories and pthreads (host threads with their own context and guest stack) are emulated, sockets always fail, `setjmp`/`longjmp` are handled by the recompiler.
- A thread that joins itself is detached instead of failing with `EDEADLK`: the game's network worker does that when its callback destroys its owner, and libstdc++ would throw.
- GLES2 calls go to the host's GLES2 context (SDL). Client-side vertex arrays are read from guest memory.
- `AAssetManager` reads the extracted `assets/`. `recomp::vfs_add_asset_patch` changes files as they are opened; `recomp::vfs_add_virtual_file` serves files whose content a host callback makes on each open.

### 3.4 JNI and Java
- A fake `JNIEnv`/`JavaVM` lives in guest memory. `runtime/jni` implements the Java classes the engine calls by name and signature; unknown ones are logged and return defaults.
- `com/rovio/rcs/*`, Flurry and notifications answer as on a device without network (`offline_services.cpp`). The video player reports that videos cannot play, which the game handles.
- `java/util/Locale` reports `--locale`, `SLINGSHOT_LOCALE`, `LC_ALL`, `LC_MESSAGES` or `LANG`, else `en_US`.
- Callbacks posted to the GL thread run before the next frame.
- The host calls the entry points in the app's order: `JNI_OnLoad → nativeInit → nativeConfig → nativeResize → (nativeUpdate, nativeRender)*`.

### 3.5 Input
`platform/sdl/android_input.*` rebuilds the app's input path (`InputDelegator` → `MyInputHandler.handleEvents()`) and calls `nativeInput`, `nativeKeyInput` and `nativeInputAxis` before `nativeUpdate`.
- Touch and the left mouse button are MotionEvents (lowest free pointer id, batched per frame). A mouse without a button hovers.
- Keys use Android keycodes with AOSP's fallback keys, so Esc arrives as BACK. The scripts only see BACK and MENU.
- Gamepads report Android keycodes and axes, which the game ignores; see the `gamepad-pointer` mod.

### 3.6 Audio and lifecycle
- `AudioOutput` (`fusion_audio.cpp`) primes the whole buffer with one `nativeMixData` call, as the original, then mixes a period at a time into a FIFO that the host's audio thread pulls from. Latency is the host's (512 frames with SDL), not the original's ~370 ms.
- The host runs `NativeApplication`'s states (uninitialized, render-ready, paused, exiting). Minimizing is `onPause`, restoring `onResume`.

### 3.7 System fonts
`SystemFontRenderer` (`fusion_font.cpp`) draws text into an ARGB array as Android's `Paint`/`Canvas` do, with FreeType and HarfBuzz:
- `Typeface.create` lookup in Android's font map; unknown names get sans-serif.
- Metrics of the primary font, shaping per font run, fallback fonts for missing characters.
- Whole-pixel glyph origins, synthetic bold and italic, a miter-joined outline, unpremultiplied output.

fontconfig finds the fonts (`platform/sdl/font_provider.*`) and prefers Roboto and Noto when installed.

### 3.8 C++ exceptions
The guest's own unwinder (`__cxa_throw` → `_Unwind_RaiseException`) is recompiled like any code. Only its last step, `restore_core_regs`, is replaced: it throws a host exception with the target `pc` and `sp`. Functions with landing pads catch it, check that the target is in their frame and jump to the pad. Functions without landing pads have no overhead.

### 3.9 Plain scripts
Scripts, `config.json` and `data/config/*.zip` are AES-256-CBC encrypted and LZMA/7z compressed. The three loaders for them are wrapped (`runtime/hle/fusion/script_loader.cpp`): a plain file is returned as it is, anything else goes through the original code. A file is plain if it is not whole AES blocks, starts with `\x1bLua` or `PK\3\4`, or its first 256 bytes have no control characters except tab and line breaks (an encrypted file passes with a chance of about 1e-12). Plain files skip the hash check. `--dump-scripts` runs the original loaders on every file and writes the result. Sprite sheet descriptions use another loader that is not wrapped.

## 4. Mods

The base port behaves like the original. Everything else is a mod: `mods/<name>/`, registered with `SLINGSHOT_MOD` (`platform/sdl/mod.hpp`). A mod sees SDL events before the host, runs once per frame before input is delivered, can draw over the frame (`Overlay`) and produces input only through `AndroidInput`.

Most mods patch scripts with `append_lua(script, code)` (`mods/common/game_scripts.hpp`): the script is served as Lua source that runs the original chunk, then the mod's code, in the script's environment, with `_G` as fallback. The original chunk is decrypted by the game's own cipher object, with the key the game builds at run time. Per-frame Lua hooks wrap `isSafeToQuit`, the first call of the game's update. Host and scripts exchange data through virtual files.

- **wheel-zoom**: in a level, an emulated pinch around the cursor. In menus it drives the PC version's wheel handling, which is still in the scripts (`cursor.wheel`, `cursor.wheelTriggered` → `'WHEEL'` pointer events). `SmartScrollFrame` moves by the notches, once per wheel event (`cursor.wheelId`).
- **keyboard**: key state comes from the virtual file `cache/slingshot-keys.lua`; the script sets what the engine sets for a finger (`cursor`, `keyPressed/keyHold/keyReleased.LBUTTON`, `touchcount`). Aiming is an offset from the grab point, clamped at 1.2 × the band's length. R and Enter tap the visible restart and next buttons; Enter also sets `keyReleased.RETURN` for popups. Other keys are not passed on, as the scripts have unguarded debug keys.
- **gamepad-pointer**: stick cursor and buttons as touches and keys.
- **skip-level-end**: a tap that hits no HUD button does what `skip_button` does, once the level is won or 1.5 s after the last bird.
- **fullscreen**: F11 → `SDL_SetWindowFullscreen`.
- **hide-dead-services**: removes menu entries for web pages and online services, the Daily Challenge card, the shop's ad row, ad video offers, the world leaderboard and its trophy book page. Trophies only the server awarded are marked `deprecated` (hidden while locked); the first trophy tab goes unless one is earned or `offline-league` is on. The "Darude" trophy's link grants the trophy.
- **offline-league**:
  - The league's settings are stored under `getAccountId()`, which is `""` offline, and `saveLuaFile` drops them; the mod uses `"offline"`.
  - Saves that already lost them get the Feather League warm-up resumed: completed steps are moved on, and an egg wait that blocked all touches is skipped.
  - After the warm-up, the game would wait for the server. Instead, the warm-up's own code runs one round per league from Glass to Master: six levels of the league episode `MLEXTRA`, sorted by an estimate of 5000 per pig and 500 per block. Score targets are 0.6 + 0.1 × league of the estimate, with +20 % and +40 % for the 2nd and 3rd star, and the opponents are scaled to them. State: `offlineRounds` in the league settings.
  - Promotions award the league trophies. First place awards `legend`, a top-5 place `champion`.
  - After Master, all leagues played can be switched with arrows and dots below the panel.
  - The profile page (`StickerBookPagePlayerCard`) gets the player card, with a medal for the Master round, and the emblem editor (`offlineRounds.emblem`).
  - `canAccessLeagueShopItems` is true after the warm-up.
- **offline-shop**: the currency popup keeps only the coin tab; the gem "+" and the "not enough gems" popup's shop button go. The Mighty Eagle is a coin item (`shopCatalog.mighty_eagle2`) unlocked by the purchase's own code (`Payment._onItemReceived`). Episode levels pay coins on top of `star_rewards` (scaled by the episode's position in `g_episodeIds`) and gems for new stars. League levels pay the same once per level (`settings.offlineShopLeagueStars`). More than 10 coins or gems fly in the time of 10.
- **game-statistics**: the game unlocks and stores its achievements offline (`AchievementProcessor`, `settings.achievementStatus`); Google Play only displayed them. Their texts (`TEXTS_ACHIEVEMENTS_*.dat`) are in the APK but never loaded on Android. The mod loads them, with texts of its own for those without (episodes 6–8 use episode 1's with their number). `updateDestroyedBlocksValues` only counted with a gamer service; it gets a dummy one. Unlocks show as `displayNotificationBanner` banners.
  - The progress is the mean of: stars (`calculateTotalStars`, all episodes not `ignoreInStarCounts`), golden eggs (36 levels), feathers, achievements, and the leagues with `offline-league`.
  - Everything is reachable offline. "Ready For More" (free version) and "Episode SURF" (never awarded) are not listed.
  - `checkForAchievements` re-checks most achievements from the save. "Space Invader" and "Episode 7/8 – Total Destruction" come only from their events.

## 5. Debugging

- `RECOMP_TRACE=jni,imports,unwind,assets,dl,syscalls,input,audio,fonts,scripts,mods`: per-channel traces.
- `SDL_AUDIO_DRIVER=disk`: sound output to `sdlaudio.raw` (S16LE).
- `SLINGSHOT_SCRIPT=<file>`: input events and screenshots at given frames (commands in `platform/sdl/test_script.hpp`).
- `SLINGSHOT_CONTROL=<fifo>`: the same commands while the game runs (`echo "+0 shot a.bmp" > fifo`, `+n` = n frames from now). `sync <tag>` prints `host: sync <tag> frame <n>` once all earlier commands ran. `lua <code>` runs Lua in the game and logs `lua: <result>`; the game's Lua has no `io`, `os.remove` or `debug`.
- Guest faults print the guest address, registers and a host backtrace with `fn_<addr>` names. A frame taking more than 10 s makes the watchdog print the same for the game thread.

## 6. Layout

```
tools/recompiler/   ELF and exidx parsing, analysis, C++ emitter
tools/scripts/      APK extraction, DEX inspection
runtime/core/       memory, CPU context, dispatch, loader, threads, unwinding
runtime/hle/        libc, GLES2, Android (assets, log, properties), engine hooks
runtime/jni/        fake JNIEnv and Java classes
platform/sdl/       window, input, audio, fonts, mod API, main loop
mods/               optional features, one directory each
game/               build rules for the generated code
```

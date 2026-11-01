![StepMania for PS Vita](titlelogo.png)

# StepMania 5 for PlayStation Vita

Port of the rhythm game **StepMania 5.0.12** to the PS Vita.
Toolchain: [VitaSDK](https://vitasdk.org/), graphics via [vitaGL](https://github.com/Rinnegatamante/vitaGL) (fixed-function OpenGL over GXM).

> The project began from the 5.1 branch, then was reverted to **5.0.12** — this tree corresponds to the build `StepMania5-Vita50-b112` and newer.

## Status (build 112)

Works:
- **Boots straight into the game.** The old launcher menu is gone: `sm_main` runs on the first frame.
- Loading up to menus and gameplay, sound (Vita audio driver), input from a gamepad and from the touch panel.
- **Song pack browser.** A self-contained downloader (vita2d + freetype, no engine resources) with the curated pack list, a search across the full StepManiaOnline catalogue (10 000+ packs), pack banners, a details card, download progress with cancel, and one-tap install into `ux0:data/stepmania-5.0/Songs/<Group>/`. It only opens from the LiveArea **Download Song Packs** tile, so a normal launch never sees it.
- **HTTPS downloads that work on the device.** `SceHttp`/`SceSsl` cannot complete a TLS handshake with current hosts, so the port ships **libcurl + mbedTLS** with a bundled CA store (`app0:/cacert.pem`, overridable by `ux0:data/stepmania-5.0/cacert.pem`): verification stays on, a verification-off retry covers devices whose clock makes certificates look expired, transient errors are retried over a fresh connection, and half-written files are removed.
- **LiveArea action tile.** A 282x108 "Download Song Packs" item under the gate (`frame9`) starts the app with `psla:packs`, which `sceAppUtilReceiveAppEvent()`/`sceAppUtilAppEventParseLiveArea()` hands back to the port.
- **Touch controls.** The front panel is split into four columns matching the four arrows; multitouch means jumps and holds work. Swiping up/down on the music wheel scrolls songs. Lanes and overlay can be switched off in Theme Options (`Touch Controls`, `Touch Zones`, `Touch Zone Opacity`).
- **Song folders may be named freely.** Apostrophes, spaces, `&`, brackets and similar characters are fine (`Songs/Rudy's Mix/...` loads as written). A simfile folder placed directly under `Songs/` with no group around it is now loaded as a one-song group instead of aborting the startup with a fatal error.
- **Performance.** Menus and gameplay hold a steady 60 FPS. Draw calls are batched — consecutive quads sharing a texture/blend/state are merged into a single GPU draw — and GL state is cached (texture params, blend, matrices, z-state) so the engine's repeated calls do not reach vitaGL.
- **Rendering fixes.** The old black/gradient panels and missing/incorrect textures are gone (texture-0 handling was wrong).
- **Error dialogs.** Pack browser failures open a system dialog (`SceMsgDialog`) with the message instead of failing silently.
- **Empty wheel protection.** If the stage filter would drop every song, the unfiltered list is shown and the reason is logged.
- **Ready-to-play defaults.** 1 player centred, 2-player styles hidden, texture resolution capped at 512 — the cap is enforced even when an older `Preferences.ini` asks for more (see [Vita graphics defaults](#vita-graphics-defaults)).
- **Button prompts** (arrows, Start/Select/Back) redrawn in PS Vita style.
- Custom **loading screen**: artwork + brown window, XP-style progress segments, stage labels in the Silkscreen font, and the current song/pack details.
- **Fatal error screen**: when crashing during the loading stage, it shows the error message and a screenshot of the last rendered frame.
- Redesigned **LiveArea** background with the StepMania logo, plus a **Recent songs** frame refreshed after every stage.

Known limitations:
- The pack browser needs a network connection: the catalogue is fetched from this repository (`packs.json`, `packs-all.json`) and packs are downloaded from their StepManiaOnline URLs.
- Song wheels with very large packs can still dip below 60 FPS while scrolling.
- CDRAM (video memory) is almost full — this is normal as long as textures do not disappear. Default graphics have already been compressed (see notes below).
- Touch lanes cover the four arrow columns only: no per-lane calibration, no slides.

## Installation (for players)

1. Grab `StepMania5-Vita50-bXX.vpk` from [Releases](../../releases) and install it via VitaShell.
2. The first launch unpacks the data (~3 minutes on the loading screen) — wait for the menu.
3. Put songs in a **group folder**:
   ```
   ux0:data/stepmania-5.0/Songs/<PackName>/<SongName>/
       song.ssc (or .sm), music.ogg, banner.png, background.png
   ```
   Group and song folder names may contain apostrophes, spaces and other usual characters (`Rudy's Mix` works as-is). A song folder placed directly under `Songs/` with no group is loaded as a one-song group, so it no longer aborts the startup.
4. Packs can also be installed from the console: on the LiveArea tap **Download Song Packs** (the tile under the gate), pick a pack and let it unpack into `ux0:data/stepmania-5.0/Songs/`.
5. When updating to a new build, delete `ux0:data/stepmania-5.0/Save/Preferences.ini` so the new default graphics settings are applied ([see below](#vita-graphics-defaults)).
6. Logs for bug reports: `ux0:data/stepmania-5.0/Logs/log.txt`, `ux0:data/stepmania-5.0/vita-boot.log` and (for the downloader) `ux0:data/stepmania-5.0/launcher.log`.

TITLE_ID: `SM50VITA1`.

## Controls

| Vita button | Action |
|---|---|
| D-Pad | Navigate menus / arrow panels |
| Touch panel | Gameplay: the four columns are the four arrows (multitouch supported). Song wheel: swipe up/down to scroll |
| △ / ✕ / □ / ○ | Arrow panels: Up / Down / Left / Right |
| START | Confirm (Enter) |
| SELECT | Select |
| L / L1 | Back |
| R / R1 | *unbound* |

## Song packs and the downloader

The browser reads its curated index from `packs.json` in this repository (override with a local `ux0:data/stepmania-5.0/packs.json`), and the full catalogue from `packs-all.json` the first time you search. Both are plain JSON; the format is described in [PACKS.md](PACKS.md).

- Downloads are HTTPS via libcurl + mbedTLS, with the CA store shipped in the VPK.
- A pack is unpacked straight into `ux0:data/stepmania-5.0/Songs/<Group>/` and the archive is deleted afterwards.
- The downloader keeps its own log: `ux0:data/stepmania-5.0/launcher.log`.

## Vita graphics defaults

(`src/PrefsManager.cpp`, section `#if defined(VITA)` — adjustable from Options → Graphics)

| Parameter | Vita default | Why |
|---|---|---|
| `MaxTextureResolution` | 512 | Saves CDRAM; 960x544 is enough for the screen |
| `HighResolutionTextures` | ForceOff | No `(doubleres)` pages for fonts |
| `FastNoteRendering` | On | Avoids `ClearZBuffer` for every note (tile-based GPU) |
| `TextureColorDepth` | 32 | The backend still fills RGBA8888 anyway — 16-bit would not save VRAM |

## Building from source

Requirements: VitaSDK (environment variable `VITASDK`), vitaGL from VDPM, CMake, Ninja.

```sh
cmake -S . -B Build-Vita/build -G Ninja \
  --toolchain $VITASDK/share/vita.toolchain.cmake \
  -C Build-Vita/vita-preset.cmake
cmake --build Build-Vita/build
```

Packaging a VPK manually:
```sh
arm-vita-eabi-strip stepmania -o stepmania.stripped   # for Reference; vita-elf-create requires the non-stripped ELF
vita-elf-create stepmania stepmania.velf
vita-make-fself -s stepmania.velf eboot.bin
# VPK = zip: eboot.bin + sce_sys/* + data.zip + bootart/*
```

`bootart/` (background `bg.raw` 960x544 RGBA + prerendered stages + font) is built from `arts/` and `fonts/` with the scripts `build-bootart-assets.py`, `build-bootfont.py`, and `build-bootart.sh`.

## Vita port structure

| Path | Purpose |
|---|---|
| `src/arch/Vita/` | `ArchHooks_Vita` (tree data, unpacking `data.zip`, boot labels), `LowLevelWindow_Vita` (vglInit 960x544), `RageDisplay_Vita` (fixed-function pipeline, batched draw, framebuffer render), `Audio_Vita`, `Input_Vita` |
| `src/arch/LoadingWindow/LoadingWindow_Vita.*` | Bridge from `SongManager` progress to the splash screen |
| `src/CMakeData-vita.cmake`, `Build-Vita/` | Build integration, build counter |
| `arts/`, `fonts/` | Source assets for the splash (`LDSM4V.png`, `slkscr.ttf`) |
| `titlelogo.png` | Logo for the README |

Key Vita-specific engine fixes: ordering `MESSAGEMAN` → `GAMESTATE`, null-guard `BroadcastOnChange::Set`, seeding yarrow without `/dev/urandom`, skipping the `song` crash on some save files, and loading `data.zip` before the game manager starts.

## English summary

StepMania 5.0.12 port to PlayStation Vita (VitaSDK + vitaGL). Boots straight into the game with sound, gamepad and touch input; menus and gameplay run at a steady 60 FPS thanks to batched draw calls and a GL state cache. A LiveArea tile opens a song-pack browser that downloads and unpacks packs over HTTPS (libcurl + mbedTLS), so packs can be installed without a PC. Custom framebuffer boot splash with progress and a fatal-error screen. Installation is via a VPK and data is placed under `ux0:data/stepmania-5.0`; in-game graphics defaults are tuned for Vita memory and display constraints.

## Credits

- StepMania team — the engine.
- Rinnegatamante — vitaGL, VitaSDK team — toolchain.
- Splash font: Silkscreen by Jason Kottke.

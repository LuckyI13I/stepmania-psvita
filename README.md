![StepMania for PS Vita](titlelogo.png)

# StepMania 5 for PlayStation Vita

Port of the rhythm game **StepMania 5.0.12** to the PS Vita.
Toolchain: [VitaSDK](https://vitasdk.org/), graphics via [vitaGL](https://github.com/Rinnegatamante/vitaGL) (fixed-function OpenGL over GXM).

> The project began from the 5.1 branch, then was reverted to **5.0.12** — this tree corresponds to the build `StepMania5-Vita50-b45` and newer.

## Status (build 45)

Works:
- Loading up to menus and gameplay, sound (Vita audio driver), input from a gamepad.
- **Performance:** menus and gameplay now hold a steady 60 FPS. Draw calls are batched — consecutive quads sharing a texture/blend/state are merged into a single GPU draw.
- **Rendering fixes:** the old black/gradient panels and missing/incorrect textures are gone (texture-0 handling was wrong).
- **Ready-to-play defaults:** 1 player centred, 2-player styles hidden, texture resolution capped for memory (see [Vita graphics defaults](#vita-graphics-defaults)).
- **Button prompts** (arrows, Start/Select/Back) redrawn in PS Vita style.
- Custom **loading screen**: artwork + brown window, XP-style progress segments, stage labels in the Silkscreen font, and the current song/pack details.
- **Fatal error screen**: when crashing during the loading stage (for example, an invalid song layout), it shows the error message and a screenshot of the last rendered frame.
- Redesigned **LiveArea** background with the StepMania logo.

Known limitations:
- Song wheels with very large packs can still dip below 60 FPS while scrolling.
- CDRAM (video memory) is almost full — this is normal as long as textures do not disappear. Default graphics have already been compressed (see notes below).

## Installation (for players)

1. Grab `StepMania5-Vita50-bXX.vpk` from [Releases](../../releases) and install it via VitaShell.
2. The first launch unpacks the data (~3 minutes on the loading screen) — wait for the menu.
3. Put songs in **groups only**:
   ```
   ux0:data/stepmania-5.0/Songs/<PackName>/<SongName>/
       song.ssc (or .sm), music.ogg, banner.png, background.png
   ```
   A song folder **without a group** at the root of `Songs/` results in a silent exit during loading (the engine requires a group).
4. When updating to a new build, delete `ux0:data/stepmania-5.0/Save/Preferences.ini` so the new default graphics settings are applied ([see below](#vita-graphics-defaults)).
5. Logs for bug reports: `ux0:data/stepmania-5.0/Logs/log.txt` and `ux0:data/stepmania-5.0/vita-boot.log`.

TITLE_ID: `SM50VITA1`.

## Controls

| Vita button | Action |
|---|---|
| D-Pad | Navigate menus / arrow panels |
| △ / ✕ / □ / ○ | Arrow panels: Up / Down / Left / Right |
| START | Confirm (Enter) |
| SELECT | Select |
| L / L1 | Back |
| R / R1 | *unbound* |

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

StepMania 5.0.12 port to PlayStation Vita (VitaSDK + vitaGL). Boots to menu and gameplay with sound and gamepad input; menus and gameplay run at a steady 60 FPS thanks to batched draw calls. Custom framebuffer boot splash with progress and a fatal-error screen. Installation is via a VPK and data is placed under `ux0:data/stepmania-5.0`. In-game graphics defaults are tuned for Vita memory and display constraints.

## Credits

- StepMania team — the engine.
- Rinnegatamante — vitaGL, VitaSDK team — toolchain.
- Splash font: Silkscreen by Jason Kottke.

# StepMania 5 for PlayStation Vita

A port of StepMania 5.1 to the PS Vita. Toolchain: VitaSDK, graphics via vitaGL (fixed-function OpenGL on top of sceGxm).

**Current status:** the game builds into a VPK, launches on hardware, completes loading (theme, sounds, noteskins) and renders the UI at 60 FPS. In progress: input, audio, memory diet, gameplay.

This is a fork of [StepMania](https://github.com/stepmania/stepmania) (`5_1-new`); all original sources, themes and assets remain in place.

## Requirements

- Windows + [VitaSDK](https://vitasdk.org/) (`VITASDK` must point at the SDK root)
- CMake 3.20+, Git, Python 3
- VitaSDK packages (via `vdpm` in Git Bash):
  `vitagl vitaGL vitashark libmathneon libogg libvorbis libjpeg-turbo`

## Building

```powershell
$env:VITASDK = "C:\path\to\vitasdk"

# 1. Configure (Ninja generator, ninja.exe lives in Build-Vita/tools/)
cmake -S . -B Build-Vita/build `
  --toolchain "$env:VITASDK/share/vita.toolchain.cmake" `
  -C Build-Vita/vita-preset.cmake -DVITA=ON -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=<repo>/Build-Vita/tools/ninja.exe"

# 2. Compile
ninja -C Build-Vita/build -j 4

# 3. Package (ELF -> VELF -> eboot.bin -> VPK), see Build-Vita/build-vita.ps1
```

Every build increments the counter in `Build-Vita/build-number.txt`; the number goes into the log (`StepMania Vita build N`) and into `param.sfo` (`APP_VER`).

First launch extracts `data.zip` (themes, data, noteskins) to `ux0:data/stepmania-5.1/` — a one-time procedure taking a few minutes. Songs are copied manually over FTP to `ux0:data/stepmania-5.1/Songs/`. Later code updates only need the new `eboot.bin` copied over `ux0:app/SM5VITA01/`; reinstalling the VPK is unnecessary.

## Controls (Vita -> keyboard events)

| Vita button | Action |
|---|---|
| D-Pad | Menu arrows |
| Cross / Start | Confirm |
| Circle | Back |
| Triangle / Square | Tab / Space |
| Select | F1 (coin) |
| L / R (and L2 / R2) | Left / Right |

## Key port files

| Path | Purpose |
|---|---|
| `src/arch/Vita/` | `RageDisplay_Vita` (renderer), `LowLevelWindow_Vita` (vitaGL), `InputHandler_Vita` (sceCtrl), `RageSoundDriver_Vita` (stub over sceAudioOut), `ArchHooks_Vita` (`app0:`/`ux0:` paths, data-tree creation, `data.zip` extraction) |
| `src/CMakeData-vita.cmake` | Vita platform sources and flags |
| `Build-Vita/vita-preset.cmake` | Cross-compilation preset, `Build-Vita/bump-vita-build.cmake` — build counter |
| `3rdparty/vitaGL` | vitaGL sources (reference; links against the prebuilt SDK copy) |

## Known limitations

- Audio: stub driver, no output yet.
- `MovieTexture` is always Null (background videos disabled).
- `NetworkManager` is an offline stub.
- Render targets and 3D character models are stubbed out.
- Memory footprint is high (desktop texture architecture); a memory diet is planned.
- Verified on retail Vita 1000; does not work on Vita3K (low-level GXM).

## Credits

StepMania, Rinnegatamante (vitaGL, vitaShaRK), the VitaSDK team, libtom, Xiph (ogg/vorbis).

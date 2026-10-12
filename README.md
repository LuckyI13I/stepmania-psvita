![StepMania for PS Vita](titlelogo.png)

# StepMania 5 для PlayStation Vita

Порт ритм-игры **StepMania 5.0.12** на PS Vita.
Тулчейн: [VitaSDK](https://vitasdk.org/), графика через [vitaGL](https://github.com/Rinnegatamante/vitaGL) (fixed-function OpenGL поверх GXM).

> Проект начинался с ветки 5.1, затем откачен на **5.0.12** — это дерево соответствует билду `StepMania5-Vita50-b20` и новее.

## Статус (build 20)

Работает:
- Загрузка до меню и геймплея, звук (Vita audio driver), ввод с геймпада.
- Собственный **экран загрузки**: арт + коричневое окно, XP-сегменты прогресса, подписи стадий шрифтом Silkscreen, имена загружаемых песен. Виден с первой секунды, включая распаковку `data.zip`.
- **Экран фатальной ошибки**: при вылете на этапе загрузки (например, неверная раскладка песен) показывает текст ошибки + хвост лога. Выход — кнопка ✕.
- Текстуры/RGBA, иконки кнопок, 3D-фон — исправлены (см. историю билдов ниже).

Известные ограничения:
- FPS в меню ниже, чем в геймплее (40–50 в игре). Идёт работа над батчингом отрисовки.
- CDRAM (видеопамять) почти полон — это нормально, пока текстуры не пропадают. Дефолты графики уже ужаты (см. ниже).

## Установка (для игроков)

1. Заберите `StepMania5-Vita50-bXX.vpk` из [Releases](../../releases) и установите через VitaShell.
2. Первый запуск распаковывает данные (~3 минуты на экране загрузки) — дождитесь меню.
3. Песни кладите **только группами**:
   ```
   ux0:data/stepmania-5.0/Songs/<НазваниеПака>/<НазваниеПесни>/
       song.ssc (или .sm), music.ogg, banner.png, background.png
   ```
   Папка песни **без группы** в корне `Songs/` = тихий выход при загрузке (движок требует группы).
4. При обновлении на новый билд удалите `ux0:data/stepmania-5.0/Save/Preferences.ini`, чтобы подхватились новые дефолты графики (кеймапы лежат отдельно и не пострадают).
5. Логи для багрепортов: `ux0:data/stepmania-5.0/Logs/log.txt` и `ux0:data/stepmania-5.0/vita-boot.log`.

TITLE_ID: `SM50VITA1`.

## Управление

| Кнопка Vita | Клавиша |
|---|---|
| D-Pad / L1 / R1 / триггеры | Стрелки |
| ✕ / Start | Enter |
| ○ | Esc |
| △ | Tab |
| □ | Space |
| Select | F1 |

## Графические дефолты Vita

(`src/PrefsManager.cpp`, секция `#if defined(VITA)` — меняются через Options → Graphics)

| Параметр | Vita-дефолт | Почему |
|---|---|---|
| `MaxTextureResolution` | 512 | экономия CDRAM, экрану 960x544 хватает |
| `HighResolutionTextures` | ForceOff | без `(doubleres)`-страниц шрифтов |
| `FastNoteRendering` | On | без `ClearZBuffer` на каждую ноту (тайловый GPU) |
| `TextureColorDepth` | 32 | бэкенд всё равно заливает RGBA8888 — 16 бит не сэкономило бы VRAM |

## Сборка из исходников

Требования: VitaSDK (переменная `VITASDK`), vitaGL из VDPM, CMake, Ninja.

```sh
cmake -S . -B Build-Vita/build -G Ninja \
  --toolchain $VITASDK/share/vita.toolchain.cmake \
  -C Build-Vita/vita-preset.cmake
cmake --build Build-Vita/build
```

Упаковка VPK вручную:
```sh
arm-vita-eabi-strip stepmania -o stepmania.stripped   # для Reference; vita-elf-create нужен НЕстрипнутый ELF
vita-elf-create stepmania stepmania.velf
vita-make-fself -s stepmania.velf eboot.bin
# VPK = zip: eboot.bin + sce_sys/* + data.zip + bootart/*
```

`bootart/` (фон `bg.raw` 960x544 RGBA + предрендер стадий + шрифт) собирается из `arts/` и `fonts/` скриптами `build-bootart-assets.py`, `build-bootfont.py`.

## Структура Vita-порта

| Путь | Что |
|---|---|
| `src/arch/Vita/` | `ArchHooks_Vita` (дерево данных, распаковка `data.zip`, boot-метки), `LowLevelWindow_Vita` (vglInit 960x544), `RageDisplay_Vita` (fixed-function рендер), `RageSoundDriver_Vita`, `InputHandler_Vita`, `VitaBootSplash` (экран загрузки/ошибок на raw framebuffer) |
| `src/arch/LoadingWindow/LoadingWindow_Vita.*` | мост прогресса `SongManager` → сплэш |
| `src/CMakeData-vita.cmake`, `Build-Vita/` | интеграция в сборку, счётчик билдов |
| `arts/`, `fonts/` | исходники сплэша (`LDSM4V.png`, `slkscr.ttf`) |
| `titlelogo.png` | логотип для README |

Ключевые Vita-правки движка: порядок `MESSAGEMAN` → `GAMESTATE`, null-guard `BroadcastOnChange::Set`, сидирование yarrow без `/dev/urandom`, пропуск корня `ux0:data` в `MakeDirs`, no-op `FlushDir`, маски `Swap32` под `GL_RGBA/UBYTE`, `ClearAllTextures` = unbind (не delete), subrect-offset и fast-path в `UpdateTexture`, strided `DrawTris` без vertex-soup.

## English summary

StepMania 5.0.12 port to PlayStation Vita (VitaSDK + vitaGL). Boots to menu and gameplay with sound and gamepad input. Custom framebuffer boot splash with progress and a fatal-error screen. Install the `.vpk` from Releases via VitaShell; put songs under `ux0:data/stepmania-5.0/Songs/<Group>/<Song>/` (ungrouped song folders quit the app on load by engine design). See the Russian section above for controls, Vita graphics defaults and build instructions.

## Credits

- StepMania team за движок.
- Rinnegatamante за vitaGL, VitaSDK team за тулчейн.
- Шрифт сплэша: Silkscreen by Jason Kottke.

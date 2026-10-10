# StepMania 5 для PlayStation Vita

Порт StepMania 5.1 на PS Vita. Тулы: VitaSDK, графика через vitaGL (fixed-function OpenGL поверх sceGxm).

**Текущий статус:** игра собирается в VPK, запускается на консоли, проходит загрузку (тема, звуки, нотскины), отрисовывает интерфейс на 60 FPS. В работе: ввод, звук, диета памяти, игровой процесс.

## Требования

- Windows + [VitaSDK](https://vitasdk.org/) (`VITASDK` должен указывать на корень SDK)
- CMake 3.20+, Git, Python 3
- Пакеты VitaSDK (через `vdpm` в Git Bash):
  `vitagl vitaGL vitashark libmathneon libogg libvorbis libjpeg-turbo`

## Сборка

```powershell
$env:VITASDK = "C:\path\to\vitasdk"

# 1. Конфигурация (генератор Ninja, ninja.exe лежит в Build-Vita/tools/)
cmake -S . -B Build-Vita/build `
  --toolchain "$env:VITASDK/share/vita.toolchain.cmake" `
  -C Build-Vita/vita-preset.cmake -DVITA=ON -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=<repo>/Build-Vita/tools/ninja.exe"

# 2. Компиляция
ninja -C Build-Vita/build -j 4

# 3. Упаковка (ELF -> VELF -> eboot.bin -> VPK)
#    см. шаги в Build-Vita/build-vita.ps1
```

Каждый билд инкрементит счётчик в `Build-Vita/build-number.txt`, номер попадает в лог (`StepMania Vita build N`) и в `param.sfo` (`APP_VER`).

Первый запуск распаковывает `data.zip` (темы, данные, нотскины) в `ux0:data/stepmania-5.1/` — занимает несколько минут один раз. Песни кладутся вручную по FTP в `ux0:data/stepmania-5.1/Songs/`. Последующие обновления кода — заменой одного `eboot.bin` в `ux0:app/SM5VITA01/`, переустановка VPK не нужна.

## Управление (Vita -> клавиатурные события)

| Кнопка Vita | Действие |
|---|---|
| Крестовина | Стрелки меню |
| Крест / Start | Подтвердить |
| Круг | Назад |
| Треугольник / Квадрат | Tab / Space |
| Select | F1 (монетка) |
| L / R (+ L2 / R2) | Влево / вправо |

## Ключевые файлы порта

| Путь | Назначение |
|---|---|
| `src/arch/Vita/` | `RageDisplay_Vita` (рендерер), `LowLevelWindow_Vita` (vitaGL), `InputHandler_Vita` (sceCtrl), `RageSoundDriver_Vita` (заглушка под sceAudioOut), `ArchHooks_Vita` (пути `app0:`/`ux0:`, создание дерева данных, распаковка `data.zip`) |
| `src/CMakeData-vita.cmake` | Исходники и флаги платформы Vita |
| `Build-Vita/vita-preset.cmake` | Пресет кросс-компиляции, `Build-Vita/bump-vita-build.cmake` — счётчик билдов |
| `3rdparty/vitaGL` | Исходники vitaGL (референс; линкуется предсобранная из SDK) |

## Известные ограничения

- Звук: драйвер-заглушка, вывода пока нет.
- `MovieTexture` всегда Null (видеофон/BG-видео отключены).
- Сеть (`NetworkManager`) — офлайн-заглушка.
- Рендер-target'ы и 3D-модели персонажей — заглушки.
- Потребление памяти высокое (десктопная архитектура текстур); диета памяти запланирована.
- Проверено на retail Vita 1000; на Vita3K не работает (низкоуровневый GXM).

## Благодарности

StepMania, Rinnegatamante (vitaGL, vitaShaRK), команда VitaSDK, libtom, Xiph (ogg/vorbis).

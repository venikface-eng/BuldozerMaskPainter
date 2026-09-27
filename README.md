# Buldozer Landscape Mask Painter

[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-blue.svg)](https://github.com/venikface-eng/BuldozerMaskPainter)
[![DirectX](https://img.shields.io/badge/DirectX-11-green.svg)](https://github.com/venikface-eng/BuldozerMaskPainter)
[![Game](https://img.shields.io/badge/Target-DayZ%20Buldozer-orange.svg)](https://github.com/venikface-eng/BuldozerMaskPainter)

[English](#english) | [Русский](#русский)

---

<a name="english"></a>
## English

An interactive in-game landscape mask painting tool for **DayZ Buldozer** (Terrain Builder) powered by a DirectX 11 hook and Dear ImGui.

It allows terrain creators to paint, preview, and edit surface masks in real-time directly on the 3D landscape or via an interactive 2D minimap canvas, without constantly restarting Buldozer or converting textures in external image editors.

### Key Features
- **Real-Time 3D Painting**: Paint surface masks directly onto the 3D terrain mesh under the cursor.
- **Interactive 2D Mini-Map**: 1:1 pixel inspector with zoom, panning, grid, and direct painting capability.
- **Layers Integration**: Automatically parses `layers.cfg` to load texture layer colors and surface names.
- **Isolated Tool Mode**: Switch to mask painting mode (`F`) to paint freely without accidentally moving objects or flattening heightmap geometry.
- **Non-Destructive Workflow**: Paint on an overlay buffer with live base/overlay composition, eraser support, and undo history (`Ctrl + Z`).
- **Export & Saving**: Save merged 8-bit indexed `.bmp` masks ready for Terrain Builder.
- **Standalone Injector**: Built-in `BuldozerInjector.exe` for safe and easy process injection.

### Hotkeys & Controls

| Shortcut | Description |
| :--- | :--- |
| `INSERT` or `F11` | Toggle main ImGui control panel |
| `F` | Toggle Mask Painting Mode (disables object/heightmap editing) |
| `Left Click` / `ALT + Click` | Paint on landscape or mini-map |
| `E` | Toggle Eraser (restores original base mask underneath) |
| `Ctrl + Z` | Undo last brush stroke |
| `Ctrl + S` | Save merged 8-bit mask |
| `Ctrl + M` | Toggle interactive Mini-Map window |
| `Ctrl + END` | Clean uninject DLL and restore original hooks |

### Building from Source

#### Prerequisites
- **Windows 10/11 (64-bit)**
- **Visual Studio 2022** (with *Desktop development with C++* and *Windows 10/11 SDK*)
- **CMake** (3.15 or newer, included with Visual Studio)

#### Build Steps
1. Clone the repository:
   ```cmd
   git clone https://github.com/venikface-eng/BuldozerMaskPainter.git
   cd BuldozerMaskPainter
   ```
2. Run `build.bat`:
   ```cmd
   build.bat
   ```
3. Compiled binaries will be output to `bin\Release\`:
   - `BuldozerMaskPainter.dll`
   - `BuldozerInjector.exe`

### How to Use
1. Launch **Buldozer** (via Terrain Builder).
2. Run `BuldozerInjector.exe` as Administrator.
3. Once injected, the ImGui overlay will appear in Buldozer.
4. Load your `layers.cfg` and base mask file in the control panel.
5. Press `F` to enter painting mode and start drawing on the terrain!

---

<a name="русский"></a>
## Русский

Интерактивный инструмент для рисования масок ландшафта в реальном времени внутри **DayZ Buldozer** (Terrain Builder) на базе DirectX 11 хука и Dear ImGui.

Позволяет картоделам рисовать, предпросматривать и редактировать маски поверхностей прямо на 3D-ландшафте или через интерактивную 2D-миникарту без постоянных перезапусков Бульдозера и ручного пересохранения текстур во внешних графических редакторах.

### Основные возможности
- **Рисование по 3D-ландшафту**: Нанесение маски непосредственно на геометрию карты под курсором мыши.
- **Интерактивная 2D-миникарта**: 1:1 попиксельный инспектор с зумом, перемещением, сеткой и возможностью рисовать прямо на холсте миникарты.
- **Парсинг layers.cfg**: Автоматическое считывание цветов и названий слоев поверхностей из файла конфигурации карты.
- **Изолированный режим кисти**: Переключение в режим рисования масок (`F`), исключающий случайное перемещение объектов или деформацию высот ландшафта стандартными инструментами Бульдозера.
- **Неразрушающее редактирование**: Рисование в отдельный слой оверлея, поддержка ластика (восстанавливает исходную маску под кистью) и отмена действий (`Ctrl + Z`).
- **Экспорт результата**: Сохранение финальной 8-битной индексированной `.bmp` маски, полностью готовой для Terrain Builder.
- **Собственный инжектор**: Удобный `BuldozerInjector.exe` для внедрения DLL в процесс Бульдозера.

### Горячие клавиши и управление

| Клавиша | Описание |
| :--- | :--- |
| `INSERT` или `F11` | Скрыть / показать главную панель управления ImGui |
| `F` | Включить режим рисования масок (блокирует смещение объектов и рельефа) |
| `ЛКМ` / `ALT + ЛКМ` | Рисование по ландшафту или на холсте миникарты |
| `E` | Режим ластика (стирает оверлей и возвращает исходную маску) |
| `Ctrl + Z` | Отмена последнего мазка кисти |
| `Ctrl + S` | Сохранить сведенную 8-битную маску в файл |
| `Ctrl + M` | Открыть / закрыть окно интерактивной миникарты |
| `Ctrl + END` | Безопасная выгрузка DLL и восстановление хуков |

### Сборка проекта

#### Требования
- **Windows 10/11 (x64)**
- **Visual Studio 2022** (с компонентами *«Разработка классических приложений на C++»* и *Windows SDK*)
- **CMake** (3.15 или новее, доступен в составе Visual Studio)

#### Инструкция по сборке
1. Склонируйте репозиторий:
   ```cmd
   git clone https://github.com/venikface-eng/BuldozerMaskPainter.git
   cd BuldozerMaskPainter
   ```
2. Запустите скрипт сборки:
   ```cmd
   build.bat
   ```
3. Скомпилированные файлы появятся в директории `bin\Release\`:
   - `BuldozerMaskPainter.dll` — основная библиотека
   - `BuldozerInjector.exe` — консольный инжектор

### Инструкция по запуску
1. Запустите **Buldozer** (из Terrain Builder).
2. Запустите `BuldozerInjector.exe` от имени администратора.
3. После инжект в окне Бульдозера появится меню оверлея.
4. Укажите пути к вашему `layers.cfg` и файлу маски.
5. Нажмите клавишу `F` для включения режима кисти и начинайте рисовать!

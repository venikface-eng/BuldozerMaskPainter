# Buldozer Landscape Mask Painter

Инструмент для рисования масок ландшафта в реальном времени внутри DayZ Buldozer через DirectX 11 хук и Dear ImGui.

## Возможности
- Инъекция в процесс Buldozer / DayZ Diag через собственный инжектор (`BuldozerInjector.exe`)
- Оверлей на базе ImGui с поддержкой настроек кисти, слоев и миникарты
- Интеграция с `layers.cfg` для парсинга и назначения текстур слоев ландшафта
- Поддержка горячих клавиш:
  - `Ctrl + END` — выгрузка DLL и очистка хуков

## Сборка проекта
Для сборки требуется **Visual Studio 2022** (с компонентами C++ и Windows SDK) и **CMake**:
1. Запустите файл `build.bat`
2. Скомпилированные файлы появятся в папке `bin\Release\`:
   - `BuldozerMaskPainter.dll`
   - `BuldozerInjector.exe`

## Использование
1. Запустите Buldozer / Terrain Processor / DayZ Diag.
2. Запустите `BuldozerInjector.exe` от имени администратора.
3. Управляйте кистью и палитрой через появившийся оверлей.

# Measurement Instrument Control

Небольшое настольное приложение на C. Основа — пример
`backend_test/example_glfw_opengl3`; зависимости — cimgui, Dear ImGui,
GLFW и OpenGL. Исходники библиотек не изменены.

Нужны CMake >= 3.15, компиляторы C/C++ и платформенные библиотеки OpenGL.
Dear ImGui уже находится в подмодуле `imgui`. При новом клонировании:

```sh
git submodule update --init --recursive
```

Из корня репозитория:

```sh
cmake -S measurement_app -B build/measurement -DCMAKE_BUILD_TYPE=Debug
cmake --build build/measurement --parallel
./build/measurement/measurement_control
```

В текущей среде CMake установлен только во временную папку. Точные команды
для неё (вместо обычной команды `cmake`):

```sh
/private/tmp/measurement-cmake/cmake/data/bin/cmake -S measurement_app -B build/measurement -DCMAKE_BUILD_TYPE=Debug
/private/tmp/measurement-cmake/cmake/data/bin/cmake --build build/measurement --parallel 4
./build/measurement/measurement_control
```

Проверено на macOS ARM64, Apple Clang 21: сборка прошла, настольное окно
запущено с OpenGL 4.1, `--smoke-test` завершился с кодом 0 после 120 кадров
и изменения размера. Обычный запуск также создал видимое системное окно.
Визуальная проверка снимка не завершена: системный захват окна недоступен,
а дополнительный запуск для захвата OpenGL-буфера не был разрешён.

Если GLFW >= 3.3 установлен и найден CMake, используется он. Иначе CMake
скачивает и собирает GLFW 3.4 (нужен доступ к GitHub). На macOS нужны
Xcode Command Line Tools. На Linux также нужны системные зависимости GLFW
для выбранного оконного окружения (X11/Wayland) и OpenGL. При отсутствии
сети можно указать уже скачанные исходники GLFW:
`-DFETCHCONTENT_SOURCE_DIR_GLFW=/absolute/path/to/glfw`.

Автоматическая проверка в графической среде:

```sh
./build/measurement/measurement_control --smoke-test
```

Она рисует 120 кадров, проверяет наличие геометрии ImGui и ошибки OpenGL,
изменяет размер окна и закрывает его через обычный цикл освобождения ресурсов.
Ручная проверка: открыть селектор, изменить размер окна, убедиться, что
кнопки отключены и данных нет, затем закрыть окно системной кнопкой.

`main.c` отвечает за окно, события и рендеринг. `instrument_ui.c` рисует
интерфейс. `instrument_ui.h` содержит `InstrumentState`: будущий модуль
AKIP-2205 будет передавать сюда реальные режим, значение и единицу, после
чего устанавливать `has_measurement`. Обновлять состояние нужно в потоке GUI;
данные из рабочего потока следует доставлять через очередь.

Сейчас состояние пустое, подключение и сбор отключены. Приложение не
обращается к USB/портам, не читает внешние измерения и не создаёт тестовые
показания. История — пустые оси без ImPlot: в этом checkout есть обёртка
cimplot, но нет исходников её зависимости `implot`.

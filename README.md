# HUB75 Desktop Raster Simulator

A desktop C++ simulation of a 64x32 HUB75-style LED matrix with two joysticks and four face buttons.

The application is deliberately split so `src/main.cpp` looks like firmware: it contains `setup()` and `loop()`, reads inputs through `Hardware` functions, invokes a CPU rasterizer, and presents a completed LED frame. SDL2 exists only behind the desktop hardware abstraction.

## Windows build

Install:

- CMake 3.24 or newer
- Visual Studio 2022 Build Tools or Visual Studio 2022 with **Desktop development with C++**
- Git is not required; CMake downloads the SDL2 source archive directly

Then double-click:

```text
build_windows.bat
```

The first configuration downloads and builds SDL2 2.30.12. The finished executable is:

```text
build/bin/Hub75Simulator.exe
```

You can also build manually:

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel
.\build\bin\Hub75Simulator.exe
```

## Controls

| Input | Gamepad | Keyboard |
|---|---|---|
| Joystick 1 | Left stick | W/A/S/D |
| Joystick 2 | Right stick | Arrow keys |
| Face button 1 | A / Cross | 1 |
| Face button 2 | B / Circle | 2 |
| Face button 3 | X / Square | 3 |
| Face button 4 | Y / Triangle | 4 |
| Quit | — | Escape |

SDL2 uses its standardized GameController mapping, so Xbox-style, PlayStation-style, and many generic controllers expose the same two-stick/four-face-button interface. If a device has no SDL mapping, the simulator falls back to raw joystick axes 0/1 and 2/3 plus raw buttons 0/1/2/3. Controller hot-plugging is supported.

## Project layout

```text
include/Hardware.h                 Replaceable hardware abstraction
include/RasterRenderer.h           Renderer-facing input structure
src/main.cpp                       Firmware-style setup() and loop()
src/RasterRenderer.cpp             64x32 CPU rasterizer and pixel predicates
src/platform/HardwareSDL.cpp       SDL2 input, framebuffer, and window backend
src/platform/DesktopEntry.cpp      Desktop-only main() that calls setup()/loop()
CMakeLists.txt                     Downloads SDL2 and builds the executable
```

## Moving the same application to an ESP32/HUB75 panel

Keep these files largely unchanged:

```text
src/main.cpp
src/RasterRenderer.cpp
include/RasterRenderer.h
```

Replace `src/platform/HardwareSDL.cpp` with an embedded implementation of the functions declared in `include/Hardware.h`:

- `readJoystick()` should return ADC-derived values normalized to `[-1,+1]`.
- `readFaceButton()` should read the four GPIO inputs.
- `setLed()` should write RGB values into a 64x32 RGB565 or library-owned framebuffer.
- `presentLeds()` should copy/swap that framebuffer to the HUB75 DMA display.
- `millis()` and `delayMs()` can forward to the Arduino equivalents.

On Arduino/ESP32, the framework supplies the actual entry point and calls `setup()` and `loop()`, so `DesktopEntry.cpp` is omitted from the embedded build.

## Renderer behavior

`RasterRenderer.cpp` performs shader-style CPU rasterization:

1. Iterate over every physical 64x32 pixel.
2. Convert each pixel center to continuous coordinates.
3. Evaluate ellipse, circle, ring, and wave predicates.
4. Supersample each physical pixel on a 2x2 grid.
5. Call `Hardware::setLed()` once for the final pixel color.

The included eye animation is only an example renderer. Replace `shadeSample()` while preserving the pixel loop and hardware abstraction to build other signed-distance, predicate-based, or procedural effects.

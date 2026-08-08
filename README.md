# HUB75 Raster Visualizer

A C++ 64x32 raster visualizer with two joysticks and four face buttons. Windows
uses an SDL window automatically. Linux uses Raspberry Pi GPIO and a HUB75
panel automatically; SDL remains active for controller input but creates no
visual window.

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

Pass `-l` to log every raw button press/release, hat change, and raw axis
change from axis 4 onward. The two main sticks on axes 0–3 are excluded:

```powershell
.\build\bin\Hub75Simulator.exe -l
```

## Raspberry Pi 4B / Ubuntu build

Connect one 64x32 HUB75 panel through an Adafruit RGB Matrix Bonnet (the
`adafruit-hat` GPIO mapping). On a 64-bit or 32-bit Raspberry Pi Ubuntu image:

```bash
sudo apt update
sudo apt install -y build-essential cmake git libsdl2-dev
bash build_pi.sh
sudo ./build-pi/bin/Hub75Simulator
```

The first build downloads and compiles
[`rpi-rgb-led-matrix`](https://github.com/hzeller/rpi-rgb-led-matrix). The Pi 4
backend defaults to a GPIO slowdown of 4, a chain length of 1, and one parallel
panel. Root is needed for direct GPIO access. The process retains those
privileges so SDL controller hot-plugging continues to work; run only this
trusted binary.

Linux starts with real HUB75 output when no arguments are supplied:

```bash
sudo ./build-pi/bin/Hub75Simulator
```

Pass `-d` to use SDL visuals instead. On the Pi this opens the visualizer
fullscreen while keeping the same SDL controller input:

```bash
./build-pi/bin/Hub75Simulator -d
```

Pass `-t` to enable the four-core rasterizer. Without it, rendering remains
single-threaded. Flags can be combined in either order:

```bash
./build-pi/bin/Hub75Simulator -d -t
sudo ./build-pi/bin/Hub75Simulator -t
```

The threaded renderer keeps three worker threads alive and asleep between
frames while the main thread handles the fourth slice; it does not create or
join threads in the frame loop.

Use `-p` to regenerate the offline per-pass relevance masks. This samples an
80x80 X/Y grid for each joystick with blinking disabled, ignores and replaces
any existing mask, writes `raster-mask.bin` beside the executable, and exits
without initializing a display:

```bash
./build-pi/bin/Hub75Simulator -p
```

Normal runs load that file once into memory. Each individual eye, brow, mouth,
tongue, teeth, and bangs raster pass skips pixels it never changed during the
sweep. Pupil circle passes remain enabled for every pixel to cover autonomous
gaze motion. Delete the file to disable the optimization.

Pass `-f` with SDL mode to print average completed presentation FPS once per
second. It also reports the renderer backend, acceleration/VSync flags, and
active display refresh rate at startup:

```bash
./build-pi/bin/Hub75Simulator -d -f
./build-pi/bin/Hub75Simulator -d -t -f
```

SDL presentation is uncapped by default. Add `-v` to enable VSync:

```bash
./build-pi/bin/Hub75Simulator -d -v -f
```

Use `-s` instead of `-d` for a resizable SDL window whose initial client size
is the panel's actual 64×32 resolution. On Linux/Pi, `-s` also selects SDL
visual output instead of HUB75:

```bash
./build-pi/bin/Hub75Simulator -s -f
```

SDL rendering is uncapped unless VSync or a frame limit is requested. Use
`-fps <number>` to cap either SDL or HUB75 operation. The limiter sleeps for
the unused portion of each frame:

```bash
./build-pi/bin/Hub75Simulator -s -fps 60 -f
sudo ./build-pi/bin/Hub75Simulator -fps 60
```

Linux HUB75 mode defaults to a 60.5 FPS cap when `-fps` is omitted. The small
margin compensates for timer/scheduling overhead that would otherwise report
slightly below 60 FPS. An explicit `-fps` value always overrides this default;
SDL modes remain uncapped unless `-v` or a numeric `-fps` value is supplied.

Pass `-fps` without a number to run uncapped, including in HUB75 mode:

```bash
sudo ./build-pi/bin/Hub75Simulator -fps
```

When launched from SSH, `-d` automatically discovers the local Wayland or X11
desktop socket and the common Xauthority locations. Run it as the same user
that owns the logged-in desktop session; a different user may not have
permission to connect to that display.

For stable refresh, disable onboard audio (it conflicts with the Bonnet GPIO
mapping), avoid driving panel power from the Pi, and use a suitably rated 5 V
power supply with a common ground.

## Project layout

```text
include/Hardware.h                 Replaceable hardware abstraction
include/RasterRenderer.h           Renderer-facing input structure
src/main.cpp                       Firmware-style setup() and loop()
src/RasterRenderer.cpp             64x32 CPU rasterizer and pixel predicates
src/platform/Hardware.cpp          SDL2 controller input and platform lifetime
src/platform/VisualSDL.cpp         Non-Linux SDL window output
src/platform/VisualHub75.cpp       Linux HUB75 GPIO output
src/platform/VisualOutput.h        Internal visual-output interface
src/platform/DesktopEntry.cpp      Desktop-only main() that calls setup()/loop()
CMakeLists.txt                     Downloads SDL2 and builds the executable
```

## Moving the same application to another embedded target

Keep these files largely unchanged:

```text
src/main.cpp
src/RasterRenderer.cpp
include/RasterRenderer.h
```

Replace `src/platform/Hardware.cpp` and the selected visual output with an
embedded implementation of the functions declared in `include/Hardware.h`:

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

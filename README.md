# R2MEGA

<div align="center">
  ...
</div>

<br>
A Sega Mega Drive emulator written from
scratch in C++, developed alongside R2NES / R2BOY.

The project is in early development. 

## Opening a ROM

Use **File > Open** or **Ctrl+O**, then click a ROM to load it immediately.
Dragging a ROM into the window also loads it directly. ZIP archives are supported:
the first .sg, .sc, .sms or .gg entry is loaded, using its extension to select
the system. Stored and DEFLATE entries are supported; ZIP64, encrypted and
multi-disk archives are not supported. ROMs are limited to 16 MiB and ZIPs to
128 MiB.

Use **File > Reset** or **F12** to restart the current ROM, and **File > Unload**
to stop emulation and close the cartridge. Audio preferences stay unchanged.

Press **Pause/Break** to pause or resume. Opening a menu pauses emulation;
closing it resumes only if you did not pause manually.

## Display

Starts at **3X**. Choose **1X–5X** in **Display**, or press **Ctrl+1–5**.
The video fills the available area while keeping its aspect ratio.
Maximizing is allowed; manual resizing is blocked.


## Controls

Use **WASD or arrow keys** to move, **K** for button **1**, **J** for button **2**,
and **Enter** for SG-1000 Pause/Hold or Game Gear Start. **I/U** provide turbo for buttons **1/2**.

SDL-compatible gamepads are detected automatically, including connection and
removal while running. Two players are supported. See the [input guide](docs/Input.md)
for gamepad positions and keyboard mappings.


## Building

Requires **CMake 3.16+**, a **C++17 compiler** and **Git**. The first build
needs internet access to download dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

The application is generated in `bin/` (or `bin/Debug/`). See the
[build guide](docs/Building.md) for platform notes and builds without a GUI.

## Documentation


## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).

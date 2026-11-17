# R2MEGA

A Sega Mega Drive / Genesis emulator in C++, currently at the architectural
skeleton stage. No games can be run yet.

The initial target is NTSC, with cycle accuracy as a long-term goal. The core
is independent of the planned SDL3 / Dear ImGui frontend.

## Structure

- `src/Core/Board`: owns the machine components and will coordinate their clocks.
- `src/Core/Bus`: separate 68000 and Z80 bus contracts.
- `src/Core/CPU`, `VDP`, `Audio`, `Cartridge`, `IO`: hardware components.
- `src/Core/State`: reserved for save states.
- `src/Systems` and `src/Window`: reserved for application services and frontend.

The bus interfaces are starting points, not a complete model of bus timing,
arbitration, DMA or open-bus behavior. `Board::clock()` explicitly reports
that scheduling is not implemented. Component classes are placeholders.

## Building

Requires CMake 3.16+ and a C++17 compiler. The core builds without external
dependencies:

```sh
cmake -S . -B build
cmake --build build --config Debug
```

The graphical application and tests have not been implemented yet.
`R2MEGA_BUILD_GUI=ON` reports this explicitly until the frontend is ready.

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).

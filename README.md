# R2MEGA

A Sega Mega Drive / Genesis emulator in C++, currently at the architectural
skeleton stage. No games can be run yet.

The initial target is NTSC, with cycle accuracy as a long-term goal. The core
is independent of the SDL3 / Dear ImGui frontend.

## Structure

- `src/Core/Board`: owns the machine components and will coordinate their clocks.
- `src/Core/Bus`: separate 68000 and Z80 bus contracts.
- `src/Core/CPU`, `VDP`, `Audio`, `Cartridge`, `IO`: hardware components.
- `src/Core/State`: reserved for save states.
- `src/Systems` and `src/Window`: application and graphical frontend.

The bus interfaces are starting points, not a complete model of bus timing,
arbitration, DMA or open-bus behavior. `Board::clock()` explicitly reports
that scheduling is not implemented. Component classes are placeholders.

## Building

Requires CMake 3.16+, a C++17 compiler and Git. The graphical build fetches
zlib, SDL3 and Dear ImGui on first configuration:

```sh
cmake -S . -B build -DR2MEGA_BUILD_GUI=ON
cmake --build build --config Debug
```

Run the `R2MEGA` executable from the build directory. File > Open (or Ctrl+O)
loads a `.md`, `.gen`, `.bin` or `.smd` ROM, or the first supported ROM in a
`.zip` archive. SMD images with a 512-byte header and 16 KiB interleaved blocks
are converted to normal byte order. ZIP supports stored and deflated entries;
ZIP64, encrypted and multi-disk archives are not supported. ROM images are
limited to 16 MiB and ZIP files to 128 MiB. The loaded image remains in memory,
but machine emulation is not implemented yet. A temporary ROM preview shows bytes
and a partial 68000 disassembly beginning at the reset vector, with controls to
navigate by address. Unknown words are shown as `DC.W`; subsequent alignment
may be inaccurate. File > Exit closes the application.
To build only the core (which still uses zlib), configure with
`-DR2MEGA_BUILD_GUI=OFF`.

Run the ROM and window tests after building the graphical application:

```sh
ctest --test-dir build -C Debug --output-on-failure
```

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).

#include "Common/RomFile.h"
#include "Core/Cartridge/Cartridge.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <zlib.h>

using namespace R2MEGA::Common;
using R2MEGA::Core::Cartridge;
using Bytes = std::vector<uint8_t>;
static void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
static void put16(Bytes &v, uint16_t n)
{
    v.push_back(uint8_t(n));
    v.push_back(uint8_t(n >> 8));
}
static void put32(Bytes &v, uint32_t n)
{
    put16(v, uint16_t(n));
    put16(v, uint16_t(n >> 16));
}
static void set32(Bytes &v, size_t p, uint32_t n)
{
    for (int i = 0; i < 4; ++i)
        v[p + i] = uint8_t(n >> (8 * i));
}

static Bytes image()
{
    Bytes v(16 + 16384, 0);
    v[0] = 'N';
    v[1] = 'E';
    v[2] = 'S';
    v[3] = 0x1a;
    v[4] = 1;
    return v;
}

static void writeSegaHeader(Bytes &rom)
{
    std::fill(rom.begin() + 0x100, rom.begin() + 0x200, ' ');
    const auto text = [&](std::size_t offset, std::string_view value)
    { std::copy(value.begin(), value.end(), rom.begin() + offset); };
    text(0x100, "SEGA GENESIS");
    text(0x120, "DOMESTIC TITLE");
    text(0x150, "  OVERSEAS\nTITLE");
    text(0x180, "GM 12345678-01");
    rom[0x18e] = 0xab;
    rom[0x18f] = 0x34;
    text(0x1f0, " JUE ");
}

static void checkSegaHeader(const Cartridge &cartridge)
{
    const auto header = cartridge.header();
    check(header && header->console == "SEGA GENESIS" && header->domesticTitle == "DOMESTIC TITLE" &&
              header->overseasTitle == "OVERSEAS?TITLE" && header->productCode == "GM 12345678-01" &&
              header->checksum == 0xab34 && header->region == "JUE",
          "Sega header in normalized ROM");
}

static Bytes archive(const Bytes &raw, bool deflated, bool descriptor, const std::string &name = "folder/game.md")
{
    Bytes payload = raw;
    if (deflated)
    {
        z_stream s{};
        check(deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK, "deflate init");
        payload.resize(compressBound(raw.size()));
        s.next_in = const_cast<Bytef *>(raw.data());
        s.avail_in = static_cast<uInt>(raw.size());
        s.next_out = payload.data();
        s.avail_out = static_cast<uInt>(payload.size());
        check(deflate(&s, Z_FINISH) == Z_STREAM_END, "deflate");
        payload.resize(s.total_out);
        deflateEnd(&s);
    }
    const uint32_t crc = crc32(0, raw.data(), static_cast<uInt>(raw.size()));
    const uint16_t flags = descriptor ? 8 : 0, method = deflated ? 8 : 0;
    Bytes v;
    put32(v, 0x04034b50);
    put16(v, 20);
    put16(v, flags);
    put16(v, method);
    put32(v, 0);
    put32(v, descriptor ? 0 : crc);
    put32(v, descriptor ? 0 : uint32_t(payload.size()));
    put32(v, descriptor ? 0 : uint32_t(raw.size()));
    put16(v, uint16_t(name.size()));
    put16(v, 0);
    v.insert(v.end(), name.begin(), name.end());
    v.insert(v.end(), payload.begin(), payload.end());
    if (descriptor)
    {
        put32(v, 0x08074b50);
        put32(v, crc);
        put32(v, uint32_t(payload.size()));
        put32(v, uint32_t(raw.size()));
    }
    const uint32_t central = uint32_t(v.size());
    put32(v, 0x02014b50);
    put16(v, 20);
    put16(v, 20);
    put16(v, flags);
    put16(v, method);
    put32(v, 0);
    put32(v, crc);
    put32(v, uint32_t(payload.size()));
    put32(v, uint32_t(raw.size()));
    put16(v, uint16_t(name.size()));
    put16(v, 0);
    put16(v, 0);
    put16(v, 0);
    put16(v, 0);
    put32(v, 0);
    put32(v, 0);
    v.insert(v.end(), name.begin(), name.end());
    const uint32_t centralSize = uint32_t(v.size()) - central;
    put32(v, 0x06054b50);
    put16(v, 0);
    put16(v, 0);
    put16(v, 1);
    put16(v, 1);
    put32(v, centralSize);
    put32(v, central);
    put16(v, 0);
    return v;
}

static void save(const std::filesystem::path &path, const Bytes &v)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(v.data()), v.size());
    check(bool(f), "write fixture");
}

int main(int argc, char **argv)
{
    try
    {
        check(argc == 2, "fixture directory required");
        const std::filesystem::path dir = argv[1];
        std::filesystem::create_directories(dir);
        Bytes raw = image();
        writeSegaHeader(raw);
        save(dir / "game.bin", raw);
        auto cartridge = Cartridge::load(dir / "game.bin");
        check(cartridge.rom() == raw && cartridge.romName() == "game.bin", "raw ROM loading");
        checkSegaHeader(cartridge);
        save(dir / "no-header.bin", image());
        check(!Cartridge::load(dir / "no-header.bin").header(), "missing Sega header is optional");
        save(dir / "short-header.bin", Bytes(raw.begin(), raw.begin() + 0x1ff));
        check(!Cartridge::load(dir / "short-header.bin").header(), "incomplete Sega header is optional");
        for (bool deflated : {false, true})
            for (bool descriptor : {false, true})
            {
                auto zip = archive(raw, deflated, descriptor, "folder/GAME.MD");
                save(dir / "game.zip", zip);
                auto loaded = Cartridge::load(dir / "game.zip");
                check(loaded.rom() == raw && loaded.romName() == "folder/GAME.MD", "ZIP loading");
                checkSegaHeader(loaded);
            }
        const auto valid = archive(raw, true, false);
        for (size_t n = 0; n < valid.size(); ++n)
        {
            Bytes out{42};
            std::string name = "unchanged";
            check(!extractRomFromZip(Bytes(valid.begin(), valid.begin() + n), out, name) &&
                      out == Bytes{42} && name == "unchanged",
                  "truncated ZIP accepted");
        }
        auto bad = archive(raw, false, false);
        bad[30 + std::string("folder/game.md").size()] ^= 1;
        Bytes out;
        std::string name;
        check(!extractRomFromZip(bad, out, name), "bad CRC accepted");
        check(!extractRomFromZip(archive(raw, false, false, "readme.txt"), out, name), "unsupported entry accepted");
        const size_t end = valid.size() - 22;
        const size_t central = size_t(valid[end + 16]) | (size_t(valid[end + 17]) << 8) |
                               (size_t(valid[end + 18]) << 16) | (size_t(valid[end + 19]) << 24);
        bad = valid;
        set32(bad, central + 24, 0xffffffff);
        check(!extractRomFromZip(bad, out, name), "oversized ZIP entry accepted");
        bad = valid;
        bad[central + 8] |= 1;
        check(!extractRomFromZip(bad, out, name), "encrypted ZIP entry accepted");
        bad = valid;
        bad[central + 10] = 99;
        check(!extractRomFromZip(bad, out, name), "unsupported ZIP compression accepted");
        bad = valid;
        bad[30] ^= 1;
        check(!extractRomFromZip(bad, out, name), "mismatched ZIP filenames accepted");
        Bytes smd(512 + 32768);
        Bytes decoded(32768);
        for (size_t i = 0; i < decoded.size(); ++i)
            decoded[i] = uint8_t((i * 7 + i / 131) & 255);
        writeSegaHeader(decoded);
        for (size_t block = 0; block < decoded.size(); block += 16384)
            for (size_t i = 0; i < 8192; ++i)
            {
                smd[512 + block + i] = decoded[block + 2 * i + 1];
                smd[512 + block + 8192 + i] = decoded[block + 2 * i];
            }
        save(dir / "game.smd", smd);
        const auto smdCartridge = Cartridge::load(dir / "game.smd");
        check(smdCartridge.rom() == decoded, "SMD conversion");
        checkSegaHeader(smdCartridge);
        save(dir / "game.zip", archive(smd, true, false, "GAME.SMD"));
        const auto zippedSmd = Cartridge::load(dir / "game.zip");
        check(zippedSmd.rom() == decoded, "SMD in ZIP conversion");
        checkSegaHeader(zippedSmd);
        smd.pop_back();
        save(dir / "game.smd", smd);
        bool rejected = false;
        try
        {
            Cartridge::load(dir / "game.smd");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, "truncated SMD accepted");
        check(cartridge.rom() == raw, "loaded cartridge changed after failed load");
        save(dir / "empty.bin", {});
        rejected = false;
        try
        {
            Cartridge::load(dir / "empty.bin");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, "empty ROM accepted");
        std::cout << "ROM loading tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
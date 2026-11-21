#include "Core/VDP/VDP.h"
#include <cstdio>
#include <stdexcept>

using R2MEGA::Core::VDP;

static void requireAt(bool condition, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "Falha no teste dos sprites da VDP na linha %d\n", line);
        throw std::runtime_error("Falha no teste dos sprites da VDP");
    }
}
#define require(condition) requireAt((condition), __LINE__)

static void setRegister(VDP &vdp, unsigned index, unsigned value)
{
    vdp.writeControl(std::uint16_t(0x8000 | (index << 8) | value));
}

static void writeVram(VDP &vdp, unsigned address, std::uint16_t value)
{
    vdp.writeControl(std::uint16_t(0x4000 | (address & 0x3fff)));
    vdp.writeControl(std::uint16_t((address >> 14) & 3));
    vdp.writeData(value);
}

static void writeCram(VDP &vdp, unsigned index, std::uint16_t value)
{
    vdp.writeControl(std::uint16_t(0xc000 | (index << 1)));
    vdp.writeControl(0);
    vdp.writeData(value);
}

static void fillTile(VDP &vdp, unsigned tile, unsigned color)
{
    const auto pattern = std::uint16_t(color * 0x1111);
    for (unsigned row = 0; row < 8; ++row)
        for (unsigned half = 0; half < 2; ++half)
            writeVram(vdp, tile * 32 + row * 4 + half * 2, pattern);
}

static void setSprite(VDP &vdp, unsigned index, unsigned x, unsigned y,
                      unsigned size, unsigned attribute, unsigned next)
{
    const unsigned address = 0xd000 + index * 8;
    writeVram(vdp, address, std::uint16_t(y));
    writeVram(vdp, address + 2, std::uint16_t(size | next));
    writeVram(vdp, address + 4, std::uint16_t(attribute));
    writeVram(vdp, address + 6, std::uint16_t(x));
}

static void clockFrame(VDP &vdp)
{
    const auto next = vdp.frame() + 1;
    while (vdp.frame() != next)
        vdp.clock();
}

static std::uint32_t pixel(const VDP &vdp, unsigned x, unsigned y)
{
    return vdp.pixels()[y * VDP::width + x];
}

static void setup(VDP &vdp)
{
    vdp.reset();
    setRegister(vdp, 1, 0x40);
    setRegister(vdp, 2, 0x30);
    setRegister(vdp, 4, 7);
    setRegister(vdp, 5, 0x68); // Tabela de sprites em $D000.
    setRegister(vdp, 13, 0x3c);
    setRegister(vdp, 15, 2);
    writeCram(vdp, 1, 0x000e);
    writeCram(vdp, 2, 0x00e0);
    writeCram(vdp, 3, 0x0e00);
    writeCram(vdp, 4, 0x00ee);
    fillTile(vdp, 1, 1);
    fillTile(vdp, 2, 2);
    fillTile(vdp, 3, 3);
    fillTile(vdp, 4, 4);
}

int main()
{
    VDP vdp;
    setup(vdp);
    writeVram(vdp, 0xe000, 1); // Plano B vermelho atrás dos sprites.
    setSprite(vdp, 0, 128, 128, 0, 2, 0);
    clockFrame(vdp);
    require(pixel(vdp, 0, 0) == 0xff00fc00);
    require(pixel(vdp, 7, 7) == 0xff00fc00);
    require(pixel(vdp, 8, 0) == 0xff000000);

    writeVram(vdp, 2 * 32, 0x0222); // Cor zero deixa o plano aparecer.
    clockFrame(vdp);
    require(pixel(vdp, 0, 0) == 0xfffc0000);
    require(pixel(vdp, 1, 0) == 0xff00fc00);
    fillTile(vdp, 2, 2);

    writeVram(vdp, 0xe000, 0x8001); // Plano B alto cobre sprite baixo.
    clockFrame(vdp);
    require(pixel(vdp, 0, 0) == 0xfffc0000);
    setSprite(vdp, 0, 128, 128, 0, 0x8002, 0);
    clockFrame(vdp);
    require(pixel(vdp, 0, 0) == 0xff00fc00);

    setup(vdp);
    setSprite(vdp, 0, 144, 128, 0, 2, 2);
    setSprite(vdp, 2, 144, 128, 0, 3, 0);
    clockFrame(vdp);
    require(pixel(vdp, 16, 0) == 0xff00fc00); // Primeiro elo vence a sobreposição.
    require((vdp.readStatus() & 0x0020) != 0);
    require((vdp.readStatus() & 0x0020) == 0);
    setSprite(vdp, 0, 144, 128, 0, 0, 2); // Transparência revela o próximo elo.
    clockFrame(vdp);
    require(pixel(vdp, 16, 0) == 0xff0000fc);

    setup(vdp);
    fillTile(vdp, 10, 1);
    fillTile(vdp, 11, 2);
    fillTile(vdp, 12, 3);
    fillTile(vdp, 13, 4);
    setSprite(vdp, 0, 168, 168, 0x0500, 10, 0); // Dois tiles por eixo.
    clockFrame(vdp);
    require(pixel(vdp, 40, 40) == 0xfffc0000);
    require(pixel(vdp, 48, 40) == 0xff0000fc);
    require(pixel(vdp, 40, 48) == 0xff00fc00);
    require(pixel(vdp, 48, 48) == 0xfffcfc00);
    setSprite(vdp, 0, 168, 168, 0x0500, 0x180a, 0);
    clockFrame(vdp);
    require(pixel(vdp, 40, 40) == 0xfffcfc00);
    require(pixel(vdp, 55, 55) == 0xfffc0000);

    setup(vdp);
    setRegister(vdp, 12, 1); // H40: a área visível alcança o pixel 319.
    setSprite(vdp, 0, 440, 128, 0, 2, 0);
    clockFrame(vdp);
    require(pixel(vdp, 312, 0) == 0xff00fc00);
    require(pixel(vdp, 319, 0) == 0xff00fc00);

    setup(vdp);
    setSprite(vdp, 0, 128, 128, 0, 2, 1);
    setSprite(vdp, 1, 0, 128, 0, 2, 2); // X zero mascara os elos posteriores.
    setSprite(vdp, 2, 144, 128, 0, 3, 0);
    clockFrame(vdp);
    require(pixel(vdp, 0, 0) == 0xff00fc00);
    require(pixel(vdp, 16, 0) == 0xff000000);

    setup(vdp);
    for (unsigned i = 0; i < 17; ++i)
        setSprite(vdp, i, 128 + i * 8, 128, 0, 2, i == 16 ? 0 : i + 1);
    clockFrame(vdp);
    require(pixel(vdp, 120, 0) == 0xff00fc00);
    require(pixel(vdp, 128, 0) == 0xff000000);
    require((vdp.readStatus() & 0x0040) != 0);
}
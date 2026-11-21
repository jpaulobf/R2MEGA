#include "Core/VDP/VDP.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

using R2MEGA::Core::VDP;

static void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Falha no teste dos planos da VDP");
}

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

static void writeVsram(VDP &vdp, unsigned index, std::uint16_t value)
{
    vdp.writeControl(std::uint16_t(0x4000 | (index << 1)));
    vdp.writeControl(0x0010);
    vdp.writeData(value);
}

static void clockFrame(VDP &vdp)
{
    const auto next = vdp.frame() + 1;
    while (vdp.frame() != next)
        vdp.clock();
}

int main(int argc, char **argv)
{
    require(argc == 2);
    VDP vdp;
    vdp.reset();
    setRegister(vdp, 1, 0x40); // Exibição ativa.
    setRegister(vdp, 2, 0x30); // Plano A em $C000.
    setRegister(vdp, 4, 0x07); // Plano B em $E000.
    setRegister(vdp, 15, 2);
    setRegister(vdp, 16, 0);    // Planos de 32 por 32 células.
    setRegister(vdp, 13, 0x3c); // Tabela de rolagem vazia em $F000.
    writeCram(vdp, 1, 0x000e);  // Vermelho.
    writeCram(vdp, 2, 0x00e0);  // Verde.
    writeCram(vdp, 3, 0x0e00);  // Azul.
    writeCram(vdp, 4, 0x00ee);  // Amarelo.
    writeCram(vdp, 18, 0x0e0e); // Paleta 1, cor 2: magenta.

    for (unsigned row = 0; row < 8; ++row)
        for (unsigned word = 0; word < 2; ++word)
        {
            writeVram(vdp, 32 + row * 4 + word * 2, 0x1111);
            writeVram(vdp, 64 + row * 4 + word * 2, 0x2222);
        }
    writeVram(vdp, 96 + 7 * 4 + 2, 0x0004); // Canto inferior direito do padrão 3.

    for (unsigned row = 0; row < 28; ++row)
        for (unsigned column = 0; column < 32; ++column)
            writeVram(vdp, 0xe000 + 2 * (row * 32 + column),
                      std::uint16_t((row + column) & 1 ? 1 : 2));
    for (unsigned column = 0; column < 6; ++column)
        writeVram(vdp, 0xe000 + 2 * column,
                  std::uint16_t(1 | ((column == 1 || column == 2) ? 0x8000 : 0)));
    writeVram(vdp, 0xc000, 2);      // A baixa cobre B baixa.
    writeVram(vdp, 0xc002, 2);      // B alta cobre A baixa.
    writeVram(vdp, 0xc004, 0x8002); // A alta cobre B alta.
    writeVram(vdp, 0xc008, 0x1803); // Espelhamento horizontal e vertical.
    writeVram(vdp, 0xc00a, 0x2002); // Seleção da segunda paleta.

    clockFrame(vdp);
    const auto &pixels = vdp.pixels();
    require(pixels[0] == 0xff00fc00);
    require(pixels[8] == 0xfffc0000);
    require(pixels[16] == 0xff00fc00);
    require(pixels[24] == 0xfffc0000); // Índice zero é transparente.
    require(pixels[32] == 0xfffcfc00);
    require(pixels[40] == 0xfffc00fc);
    require(pixels[8 * VDP::width] == 0xfffc0000);

    const auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    std::ofstream frame(directory / "planos-a-b.ppm", std::ios::binary);
    frame << "P6\n"
          << VDP::width << ' ' << VDP::height << "\n255\n";
    for (const auto pixel : pixels)
    {
        const char rgb[3]{char(pixel >> 16), char(pixel >> 8), char(pixel)};
        frame.write(rgb, 3);
    }
    require(bool(frame));

    vdp.reset();
    require(vdp.pixels()[0] == 0xff000000);
    setRegister(vdp, 2, 0x30);
    setRegister(vdp, 4, 0x07);
    setRegister(vdp, 15, 2);
    writeCram(vdp, 1, 0x000e);
    writeVram(vdp, 32, 0x1000); // Apenas o primeiro pixel do padrão 1.
    writeVram(vdp, 0xe000, 1);
    clockFrame(vdp);
    require(vdp.pixels()[0] == 0xff000000); // Exibição desligada oculta os planos.
    setRegister(vdp, 1, 0x40);
    clockFrame(vdp);
    require(vdp.pixels()[0] == 0xfffc0000 && vdp.pixels()[1] == 0xff000000);

    VDP scroll;
    scroll.reset();
    setRegister(scroll, 1, 0x40);
    setRegister(scroll, 4, 7);
    setRegister(scroll, 13, 0x3c);
    setRegister(scroll, 15, 2);
    writeCram(scroll, 1, 0x000e);
    writeCram(scroll, 2, 0x00e0);
    writeCram(scroll, 3, 0x0e00);
    for (unsigned row = 0; row < 8; ++row)
        for (unsigned word = 0; word < 2; ++word)
        {
            writeVram(scroll, 32 + row * 4 + word * 2, 0x1111);
            writeVram(scroll, 64 + row * 4 + word * 2, 0x2222);
            writeVram(scroll, 96 + row * 4 + word * 2, 0x3333);
        }
    writeVram(scroll, 0xe000, 1);
    writeVram(scroll, 0xe002, 2);
    writeVram(scroll, 0xe004, 1);
    writeVram(scroll, 0xe040, 3); // Segunda linha de células do plano B.
    writeVram(scroll, 0xf002, 8); // Desloca B oito pixels para a direita.
    clockFrame(scroll);
    require(scroll.pixels()[0] == 0xff000000);
    require(scroll.pixels()[8] == 0xfffc0000);
    require(scroll.pixels()[16] == 0xff00fc00);

    writeVsram(scroll, 1, 8); // Desce a leitura do plano B uma célula.
    clockFrame(scroll);
    require(scroll.pixels()[8] == 0xff0000fc);
    writeVsram(scroll, 1, 0);

    setRegister(scroll, 11, 3); // Deslocamento horizontal por linha.
    writeVram(scroll, 0xf002, 0);
    writeVram(scroll, 0xf006, 8);
    clockFrame(scroll);
    require(scroll.pixels()[0] == 0xfffc0000);
    require(scroll.pixels()[VDP::width] == 0xff000000);

    setRegister(scroll, 11, 4); // Deslocamento vertical por coluna de 16 pixels.
    writeVsram(scroll, 1, 8);
    clockFrame(scroll);
    require(scroll.pixels()[0] == 0xff0000fc);
    require(scroll.pixels()[16] == 0xfffc0000);

    setRegister(scroll, 11, 0);
    writeVsram(scroll, 1, 0);
    setRegister(scroll, 16, 1); // Plano com 64 células por linha.
    writeVram(scroll, 0xe080, 2);
    clockFrame(scroll);
    require(scroll.pixels()[8 * VDP::width] == 0xff00fc00);
}
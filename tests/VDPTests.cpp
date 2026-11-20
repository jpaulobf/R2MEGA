#include "Core/VDP/VDP.h"
#include <stdexcept>

using namespace R2MEGA::Core;

static void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Falha no teste do VDP");
}

int main()
{
    VDP vdp;
    require(vdp.frame() == 0 && vdp.line() == 0 && vdp.dot() == 0);
    require(vdp.readStatus() == 0x0208);
    for (unsigned i = 0; i < 279; ++i)
        vdp.clock();
    require(vdp.dot() == 27 && vdp.readStatus() == 0x0208);
    vdp.clock();
    require(vdp.dot() == 28 && vdp.readStatus() == 0x020c);
    for (unsigned i = 280; i < 860; ++i)
        vdp.clock();
    require(vdp.dot() == 86 && vdp.readStatus() == 0x0208);
    for (unsigned i = 860; i < VDP::masterCyclesPerLine; ++i)
        vdp.clock();
    require(vdp.line() == 1 && vdp.dot() == 0);
    for (unsigned line = 1; line < VDP::visibleLines; ++line)
        for (unsigned cycle = 0; cycle < VDP::masterCyclesPerLine; ++cycle)
            vdp.clock();
    require(vdp.line() == 224 && vdp.readStatus() == 0x0208);
    for (unsigned line = 224; line < VDP::linesPerFrame; ++line)
        for (unsigned cycle = 0; cycle < VDP::masterCyclesPerLine; ++cycle)
            vdp.clock();
    require(vdp.frame() == 1 && vdp.line() == 0 && vdp.readStatus() == 0x0208);
    vdp.clock();
    vdp.reset();
    require(vdp.frame() == 0 && vdp.line() == 0 && vdp.cycleInLine() == 0);

    vdp.writeControl(0x8f02); // Incremento automático de dois bytes.
    vdp.writeControl(0x5234); // Escrita na VRAM em $1234.
    vdp.writeControl(0x0000);
    vdp.writeData(0xabcd);
    vdp.writeData(0x1234);
    require(vdp.vramByte(0x1234) == 0xab && vdp.vramByte(0x1235) == 0xcd);
    require(vdp.vramByte(0x1236) == 0x12 && vdp.dataAddress() == 0x1238);
    vdp.writeControl(0x1234); // Leitura da mesma posição.
    vdp.writeControl(0x0000);
    require(vdp.readData() == 0xabcd);

    vdp.writeControl(0x8700); // Cor de fundo: entrada zero da CRAM.
    vdp.writeControl(0xc000); // Escrita na CRAM.
    vdp.writeControl(0x0000);
    vdp.writeData(0x000e);
    require(vdp.cramWord(0) == 0x000e);
    vdp.writeControl(0x0000); // Leitura da CRAM: código 8.
    vdp.writeControl(0x0020);
    require(vdp.readData() == 0x000e);
    vdp.writeControl(0x4000); // Escrita na VSRAM: código 5.
    vdp.writeControl(0x0010);
    vdp.writeData(0x03ab);
    require(vdp.vsramWord(0) == 0x03ab);
    vdp.writeControl(0x0000); // Leitura da VSRAM: código 4.
    vdp.writeControl(0x0010);
    require(vdp.readData() == 0x03ab);
    vdp.writeControl(0x8140); // Habilita a exibição.
    require(vdp.readStatus() == 0x0200);
    for (unsigned i = 0; i <= 860; ++i)
        vdp.clock();
    require(vdp.pixels()[0] == 0xfffc0000);
    vdp.reset();
    require(vdp.cramWord(0) == 0 && vdp.vramByte(0x1234) == 0);

    VDP dma;
    dma.writeControl(0x8110); // Habilita DMA com a exibição desligada.
    dma.writeControl(0x8f01); // Incremento de um byte.
    dma.writeControl(0x9310); // Comprimento: 16 bytes.
    dma.writeControl(0x9400);
    dma.writeControl(0x9780); // Modo Fill.
    dma.writeControl(0x4000);
    dma.writeControl(0x0080);
    require(!dma.dmaActive() && (dma.readStatus() & 2));
    dma.writeData(0x12ab);
    require(dma.dmaActive() && dma.dmaRemaining() == 16);
    for (unsigned i = 0; i < 400 && dma.dmaActive(); ++i)
        dma.clock();
    require(!dma.dmaActive() && dma.dmaRemaining() == 0);
    require(dma.vramByte(0) == 0x12 && dma.vramByte(1) == 0x12);
    require(dma.vramByte(16) == 0x12 && dma.registerValue(19) == 0);

    dma.writeControl(0x9303); // Copia três bytes da posição zero para $0100.
    dma.writeControl(0x9400);
    dma.writeControl(0x9500);
    dma.writeControl(0x9600);
    dma.writeControl(0x97c0);
    dma.writeControl(0x4100);
    dma.writeControl(0x00c0);
    require(dma.dmaActive() && !dma.dmaBlocksM68K());
    for (unsigned i = 0; i < 200 && dma.dmaActive(); ++i)
        dma.clock();
    require(!dma.dmaActive() && dma.vramByte(0x100) == 0x12 && dma.vramByte(0x102) == 0x12);

    dma.setDmaReader([](std::uint32_t address)
                     { return std::uint16_t(0xa000 | address); });
    dma.writeControl(0x8f02);
    dma.writeControl(0x9302);
    dma.writeControl(0x9400);
    dma.writeControl(0x9502);
    dma.writeControl(0x9600);
    dma.writeControl(0x9700); // Origem: palavra 2 no barramento do 68000.
    dma.writeControl(0x4200);
    dma.writeControl(0x0080);
    require(dma.dmaActive() && dma.dmaBlocksM68K());
    for (unsigned i = 0; i < 100 && dma.dmaActive(); ++i)
        dma.clock();
    require(!dma.dmaActive() && dma.vramByte(0x200) == 0xa0 && dma.vramByte(0x201) == 4);
    require(dma.vramByte(0x202) == 0xa0 && dma.vramByte(0x203) == 6);
    require(dma.registerValue(21) == 4 && dma.registerValue(22) == 0);
}
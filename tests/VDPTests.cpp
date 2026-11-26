#include "Core/VDP/VDP.h"
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace R2MEGA::Core;

static void requireAt(bool condition, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "Falha no teste do VDP na linha %d\n", line);
        throw std::runtime_error("Falha no teste do VDP");
    }
}
#define require(condition) requireAt((condition), __LINE__)

int main()
{
    VDP vdp;
    require(vdp.frame() == 0 && vdp.line() == 0 && vdp.dot() == 0);
    require(vdp.readStatus() == 0x0200);
    for (unsigned i = 0; i < 279; ++i)
        vdp.clock();
    require(vdp.dot() == 27 && vdp.readStatus() == 0x0200);
    vdp.clock();
    require(vdp.dot() == 28 && vdp.readStatus() == 0x0204);
    for (unsigned i = 280; i < 860; ++i)
        vdp.clock();
    require(vdp.dot() == 86 && vdp.readStatus() == 0x0200);
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
    require(vdp.frame() == 1 && vdp.line() == 0 && vdp.readStatus() == 0x0280);
    require(vdp.readStatus() == 0x0200);
    vdp.clock();
    vdp.reset();
    require(vdp.frame() == 0 && vdp.line() == 0 && vdp.cycleInLine() == 0);

    VDP vertical;
    vertical.writeControl(0x8120); // Habilita a interrupção vertical com a imagem desligada.
    for (std::uint32_t i = 0; i < VDP::visibleLines * VDP::masterCyclesPerLine + VDP::verticalInterruptCycle; ++i)
        vertical.clock();
    require(vertical.line() == VDP::visibleLines && vertical.interruptLevel() == 0);
    require(vertical.readStatus() == 0x020c);
    vertical.clock();
    require(vertical.interruptLevel() == 6 && vertical.readStatus() == 0x028c);
    require(vertical.readStatus() == 0x020c && vertical.interruptLevel() == 6);
    vertical.acknowledgeInterrupt(6);
    require(vertical.interruptLevel() == 0);
    vertical.reset();
    require(vertical.interruptLevel() == 0 && vertical.readStatus() == 0x0200);

    VDP horizontal;
    horizontal.writeControl(0x8010); // IE1 habilita o pedido horizontal de nível 4.
    horizontal.writeControl(0x8a01); // Um pedido a cada duas linhas.
    for (unsigned i = 0; i <= VDP::horizontalInterruptCycle; ++i)
        horizontal.clock();
    require(horizontal.interruptLevel() == 4);
    horizontal.readStatus();
    require(horizontal.interruptLevel() == 4); // A leitura de estado não reconhece H-INT.
    horizontal.acknowledgeInterrupt(4);
    require(horizontal.interruptLevel() == 0);
    while (horizontal.line() != 1)
        horizontal.clock();
    for (unsigned i = 0; i <= VDP::horizontalInterruptCycle; ++i)
        horizontal.clock();
    require(horizontal.interruptLevel() == 0);
    while (horizontal.line() != 2)
        horizontal.clock();
    for (unsigned i = 0; i <= VDP::horizontalInterruptCycle; ++i)
        horizontal.clock();
    require(horizontal.interruptLevel() == 4);
    horizontal.writeControl(0x8000); // Desabilitar IE1 mascara o pedido pendente.
    require(horizontal.interruptLevel() == 0);
    horizontal.writeControl(0x8010);
    require(horizontal.interruptLevel() == 4);
    horizontal.acknowledgeInterrupt(4);
    horizontal.reset();
    require(horizontal.interruptLevel() == 0);

    auto counter = std::make_unique<VDP>();
    counter->reset();
    require(counter->readHvCounter() == 0x0000);
    for (unsigned i = 0; i < 20; ++i)
        counter->clock();
    require(counter->readHvCounter() == 0x0001);
    while (counter->cycleInLine() != 2959)
        counter->clock();
    require(counter->readHvCounter() == 0x0093);
    counter->clock();
    require(counter->readHvCounter() == 0x00e9); // H32 salta a faixa de sincronismo.
    while (counter->cycleInLine() != 3419)
        counter->clock();
    require(counter->readHvCounter() == 0x00ff);
    counter->clock();
    require(counter->readHvCounter() == 0x0100);
    while (counter->line() != 234)
        counter->clock();
    require(counter->readHvCounter() == 0xea00);
    while (counter->line() != 235)
        counter->clock();
    require(counter->readHvCounter() == 0xe500); // V recua durante o VBlank NTSC.
    while (counter->line() != 261)
        counter->clock();
    require(counter->readHvCounter() == 0xff00);
    while (counter->frame() == 0)
        counter->clock();
    require(counter->readHvCounter() == 0x0000);

    auto wideCounter = std::make_unique<VDP>();
    wideCounter->reset();
    wideCounter->writeControl(0x8c01); // H40.
    while (wideCounter->readHvCounter() != 0x00b6)
        wideCounter->clock();
    while ((wideCounter->readHvCounter() & 0xff) == 0xb6)
        wideCounter->clock();
    require(wideCounter->readHvCounter() == 0x00e4);
    while (wideCounter->cycleInLine() != 3419)
        wideCounter->clock();
    require(wideCounter->readHvCounter() == 0x00ff);

    auto fifo = std::make_unique<VDP>();
    fifo->reset();
    fifo->writeControl(0x8140); // Exibição ativa reduz os slots disponíveis.
    fifo->writeControl(0x8f02);
    fifo->writeControl(0x4000); // VRAM $0000.
    fifo->writeControl(0);
    for (unsigned i = 0; i < 4; ++i)
        require(fifo->tryWriteData(std::uint16_t(0x1100 + i)));
    require(fifo->readStatus() == 0x0100 && fifo->dataAddress() == 8);
    require(!fifo->tryWriteData(0x9999) && fifo->dataAddress() == 8);
    require(fifo->vramByte(0) == 0); // A escrita aguarda o slot da VDP.
    fifo->writeControl(0xc000);      // Alterar o comando não muda entradas já aceitas.
    fifo->writeControl(0);
    for (unsigned i = 0; i < 511; ++i)
        fifo->clock();
    require(fifo->vramByte(0) == 0x11 && fifo->vramByte(1) == 0x00);
    require((fifo->readStatus() & 0x0300) == 0);
    require(fifo->tryWriteData(0x00e0)); // A vaga livre aceita escrita na CRAM.
    for (unsigned i = 0; i < 3000 && !(fifo->readStatus() & 0x0200); ++i)
        fifo->clock();
    require((fifo->readStatus() & 0x0300) == 0x0200);
    require(fifo->vramByte(6) == 0x11 && fifo->vramByte(7) == 0x03);
    require(fifo->cramWord(0) == 0x00e0);

    auto interruptedWrite = std::make_unique<VDP>();
    interruptedWrite->writeControl(0x8f02);
    interruptedWrite->writeControl(0x50b2);
    interruptedWrite->writeControl(0x0003); // Escrita na VRAM em $D0B2.
    require(interruptedWrite->tryWriteData(0x869a));
    interruptedWrite->writeControl(0x8aff); // Escritas de registrador da H-INT.
    interruptedWrite->writeControl(0x8164); // Código $02 e endereço $C164.
    require(interruptedWrite->tryWriteData(0x869b));
    require(interruptedWrite->dataAddress() == 0xc166);
    while (interruptedWrite->cycleInLine() <= 510)
        interruptedWrite->clock();
    require(interruptedWrite->vramByte(0xd0b2) == 0x86 && interruptedWrite->vramByte(0xd0b3) == 0x9a);
    require((interruptedWrite->readStatus() & 0x0300) == 0);
    while (interruptedWrite->cycleInLine() <= 810)
        interruptedWrite->clock();
    require((interruptedWrite->readStatus() & 0x0300) == 0x0200);
    require(interruptedWrite->vramByte(0xc164) == 0 && interruptedWrite->vramByte(0xc165) == 0);
    require(interruptedWrite->cramWord(0x32) == 0);

    auto blankFifo = std::make_unique<VDP>();
    blankFifo->reset();
    blankFifo->writeControl(0xc000);
    blankFifo->writeControl(0);
    require(blankFifo->tryWriteData(0x000e));
    for (unsigned i = 0; i < 22; ++i)
        blankFifo->clock();
    require(blankFifo->readStatus() == 0x0200 && blankFifo->cramWord(0) == 0x000e);

    auto h32Slots = std::make_unique<VDP>();
    h32Slots->writeControl(0x8140);
    h32Slots->writeControl(0xc000);
    h32Slots->writeControl(0);
    require(h32Slots->tryWriteData(0x000e));
    for (unsigned i = 0; i < 230; ++i)
        h32Slots->clock();
    require(h32Slots->cramWord(0) == 0);
    h32Slots->clock(); // Primeiro slot externo H32: ciclo mestre 230.
    require(h32Slots->cramWord(0) == 0x000e);

    auto h40Slots = std::make_unique<VDP>();
    h40Slots->writeControl(0x8140);
    h40Slots->writeControl(0x8c01);
    h40Slots->writeControl(0xc000);
    h40Slots->writeControl(0);
    require(h40Slots->tryWriteData(0x000e));
    for (unsigned i = 0; i < 352; ++i)
        h40Slots->clock();
    require(h40Slots->cramWord(0) == 0);
    h40Slots->clock(); // Primeiro slot externo H40: ciclo mestre 352.
    require(h40Slots->cramWord(0) == 0x000e);

    auto contested = std::make_unique<VDP>();
    contested->setDmaReader([](std::uint32_t)
                            { return std::uint16_t(0x00e0); });
    contested->writeControl(0x8150); // Exibição e DMA habilitadas.
    contested->writeControl(0x8f02);
    contested->writeControl(0x9301);
    contested->writeControl(0x9400);
    contested->writeControl(0x9500);
    contested->writeControl(0x9600);
    contested->writeControl(0x9700);
    contested->writeControl(0xc000);
    contested->writeControl(0);
    require(contested->tryWriteData(0x000e)); // Escrita da CPU já na FIFO.
    contested->writeControl(0xc002);
    contested->writeControl(0x0080); // DMA de uma palavra para a CRAM seguinte.
    for (unsigned i = 0; i < 231; ++i)
        contested->clock();
    require(contested->cramWord(0) == 0x000e);
    require(contested->cramWord(1) == 0 && contested->dmaActive());
    for (unsigned i = 231; i < 511; ++i)
        contested->clock();
    require(contested->cramWord(1) == 0x00e0 && !contested->dmaActive());

    for (unsigned h40 = 0; h40 < 2; ++h40)
    {
        auto timedFill = std::make_unique<VDP>();
        timedFill->writeControl(0x8110);
        timedFill->writeControl(std::uint16_t(0x8c00 | h40));
        timedFill->writeControl(0x8f01);
        timedFill->writeControl(0x932c); // 300 bytes de preenchimento.
        timedFill->writeControl(0x9401);
        timedFill->writeControl(0x9780);
        timedFill->writeControl(0x4000);
        timedFill->writeControl(0x0080);
        timedFill->writeData(0x1200);
        for (unsigned cycle = 0; cycle < VDP::masterCyclesPerLine; ++cycle)
            timedFill->clock();
        // Dois slots de partida, depois 165 acessos H32 ou 203 H40.
        require(timedFill->dmaRemaining() == (h40 ? 97u : 135u));
    }

    VDP priority;
    priority.writeControl(0x8010);
    priority.writeControl(0x8120);
    for (std::uint32_t i = 0; i <= VDP::visibleLines * VDP::masterCyclesPerLine + VDP::verticalInterruptCycle; ++i)
        priority.clock();
    require(priority.interruptLevel() == 6); // V-INT prevalece sobre H-INT pendente.
    priority.acknowledgeInterrupt(6);
    require(priority.interruptLevel() == 4);
    priority.acknowledgeInterrupt(4);
    require(priority.interruptLevel() == 0);

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
    vdp.writeControl(0x404e); // Última palavra física da VSRAM.
    vdp.writeControl(0x0010);
    require(vdp.tryWriteData(0x0456));
    require(vdp.tryWriteData(0x07ff) && vdp.dataAddress() == 0x52);
    vdp.writeControl(0x407e); // Último endereço da janela de 128 bytes.
    vdp.writeControl(0x0010);
    require(vdp.tryWriteData(0x07fe));
    require(vdp.tryWriteData(0x0123) && vdp.dataAddress() == 0x82);
    require((vdp.readStatus() & 0x0300) == 0x0100);
    for (unsigned i = 0; i < 128; ++i)
        vdp.clock();
    require((vdp.readStatus() & 0x0300) == 0x0200 &&
            vdp.vsramWord(39) == 0x0456 && vdp.vsramWord(0) == 0x0123 &&
            vdp.vsramWord(1) == 0);
    vdp.writeControl(0x8140); // Habilita a exibição.
    require(vdp.readStatus() == 0x0200);
    for (unsigned i = 0; i <= 860; ++i)
        vdp.clock();
    require(vdp.pixels()[0] == 0xfffc0000);
    require(vdp.visibleWidth() == 256 && vdp.pixels()[319] == 0xff000000);
    vdp.writeControl(0x8c01); // H40: 320 pixels ativos por linha.
    const auto wideFrame = vdp.frame();
    while (vdp.frame() == wideFrame)
        vdp.clock();
    require(vdp.visibleWidth() == 320 && vdp.pixels()[319] == 0xfffc0000);
    require(vdp.pixels()[(VDP::height - 1) * VDP::width + 319] == 0xfffc0000);
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
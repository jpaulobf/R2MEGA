#include "Core/Board.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace R2MEGA::Core;

static void requireAt(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "Falha no teste da placa na linha %d\n", line);
        throw std::runtime_error("Falha no teste da placa");
    }
}
#define require(value) requireAt((value), __LINE__)

int main(int argc, char **argv)
{
    require(argc == 2);
    const auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    const auto path = directory / "board-reset.bin";
    const std::vector<unsigned char> rom{0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x08,
                                         0x4e, 0x71, 0x42, 0x54, // NOP; CLR.W (A4), com A4 em zero.
                                         0x4e, 0x71, 0x4e, 0x71};
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(rom.data()), rom.size());
        require(bool(file));
    }

    auto boardStorage = std::make_unique<Board>();
    Board &board = *boardStorage;
    require(board.masterCycles() == 0 && !board.m68k().resetVectorsLoaded());
    for (unsigned i = 0; i < 14; ++i)
        board.clock();
    require(board.masterCycles() == 14 && board.m68k().state().cycles == 2);
    board.insertCartridge(Cartridge::load(path));
    require(board.masterCycles() == 0 && !board.m68k().resetVectorsLoaded());
    require(board.tryWriteByte(1, 0x5a));
    require(board.readByte(0) == 0 && board.readWord(0) == 0x00ff);
    require(board.readByte(0xa04000) == 0 && board.readWord(0xa04000) == 0);
    board.writeByte(0xa04000, 0x22);
    board.writeByte(0xa04001, 0x5a);
    board.writeByte(0xa04002, 0x33);
    board.writeByte(0xa04003, 0xa5);
    require(board.ym2612().registerValue(0, 0x22) == 0x5a &&
            board.ym2612().registerValue(1, 0x33) == 0xa5);
    require(board.readMemory(0x4000) == 0x80 && board.readWord(0xa04000) == 0x8080 &&
            board.readByte(0xa05000) == 0x80 && board.readByte(0xa04001) == 0);
    board.writeMemory(0x4000, 0x44);
    board.writeMemory(0x4001, 0x66);
    require(board.ym2612().registerValue(0, 0x44) == 0x66);
    require(board.readWord(2) == 0xff00);
    require(board.readWord(0xa10000) == 0x00a0);
    require(board.readWord(0xc00004) == 0x0200);
    require(board.readWord(0xc00006) == 0x0200);
    require(board.readByte(0xc00004) == 0x02 && board.readByte(0xc00005) == 0);
    {
        auto counterBoard = std::make_unique<Board>();
        require(counterBoard->readWord(0xc00008) == 0);
        for (unsigned i = 0; i < 20; ++i)
            counterBoard->clock();
        require(counterBoard->readWord(0xc00008) == 1 &&
                counterBoard->readByte(0xc00008) == 0 &&
                counterBoard->readByte(0xc00009) == 1);
        counterBoard->writeWord(0xc00004, 0x8f02);
        counterBoard->writeWord(0xc00004, 0x4000);
        counterBoard->writeWord(0xc00004, 0);
        for (unsigned i = 0; i < 3; ++i)
            require(counterBoard->tryWriteWord(0xc00000, std::uint16_t(0x2000 + i)));
        require(counterBoard->tryWriteByte(0xc00003, 0x47));
        require(!counterBoard->tryWriteWord(0xc00000, 0x9999));
        require(!counterBoard->tryWriteByte(0xc00000, 0x99) && counterBoard->vdp().dataAddress() == 8);
        require((counterBoard->readWord(0xc00004) & 0x0300) == 0x0100);
        for (unsigned i = 0; i < 512 && (counterBoard->readWord(0xc00004) & 0x0100); ++i)
            counterBoard->clock();
        require(counterBoard->tryWriteByte(0xc00000, 0x53));
        for (unsigned i = 0; i < 512 && !(counterBoard->readWord(0xc00004) & 0x0200); ++i)
            counterBoard->clock();
        require(counterBoard->readWord(0xc00004) & 0x0200);
        require(counterBoard->vdp().vramByte(6) == 0x47 && counterBoard->vdp().vramByte(7) == 0x47);
        require(counterBoard->vdp().vramByte(8) == 0x53 && counterBoard->vdp().vramByte(9) == 0x53);
        counterBoard->writeByte(0xc00002, 0x66);
        require(counterBoard->vdp().vramByte(10) == 0x66 && counterBoard->vdp().vramByte(11) == 0x66);

        counterBoard->writeWord(0xc00004, 0x8110); // Habilita DMA.
        counterBoard->writeWord(0xc00004, 0x8f01); // Incremento de um byte.
        counterBoard->writeWord(0xc00004, 0x9304);
        counterBoard->writeWord(0xc00004, 0x9400);
        counterBoard->writeWord(0xc00004, 0x9780); // DMA Fill.
        counterBoard->writeWord(0xc00004, 0x4100);
        counterBoard->writeWord(0xc00004, 0x0080);
        require(counterBoard->tryWriteByte(0xc00000, 0x5a) && counterBoard->vdp().dmaActive());
        for (unsigned i = 0; i < 512 && counterBoard->vdp().dmaActive(); ++i)
            counterBoard->clock();
        require(!counterBoard->vdp().dmaActive() && counterBoard->vdp().vramByte(0x104) == 0x5a);
    }
    {
        auto interruptBoard = std::make_unique<Board>();
        interruptBoard->writeWord(0xc00004, 0x8010); // IE1 solicita H-INT de nível 4.
        for (unsigned i = 0; i <= VDP::horizontalInterruptCycle; ++i)
            interruptBoard->clock();
        require(interruptBoard->vdp().interruptLevel() == 4);
        M68KInterruptResponse response;
        require(interruptBoard->tryAcknowledgeInterrupt(4, response));
        require(response.kind == M68KInterruptResponse::Kind::Autovector &&
                interruptBoard->vdp().interruptLevel() == 0);
    }
    board.writeWord(0xc00004, 0x8f02); // Incremento automático de dois bytes.
    board.writeWord(0xc00004, 0xc000); // Endereço zero da CRAM.
    board.writeWord(0xc00006, 0x0000);
    board.writeWord(0xc00000, 0x00e0);
    require(board.vdp().cramWord(0) == 0x00e0);
    board.writeWord(0xc00004, 0x0000); // Leitura da VRAM.
    board.writeWord(0xc00004, 0x0000);
    require(board.readWord(0xc00002) == 0);
    require(board.readWord(0xa10008) == 0 && board.readWord(0xa1000c) == 0);
    require(board.readByte(0xa10003) == 0x7f);
    board.writeWord(0xa10008, 0x0040); // TH da porta 1 como saída.
    board.writeByte(0xa10003, 0);
    require(board.readWord(0xa10008) == 0x0040 && board.readByte(0xa10003) == 0x33);
    board.writeByte(0xa10008, 0xff); // Byte alto da palavra não altera o registrador.
    require(board.readWord(0xa10008) == 0x0040);
    board.writeWord(0xa10008, 0); // Devolve TH ao estado de entrada.
    require(board.readByte(0xa10003) == 0x7f);
    for (unsigned i = 0; i < 279; ++i)
        board.clock();
    require(board.m68k().resetVectorsLoaded() && !board.m68k().resetComplete() && board.m68k().state().cycles == 39);
    board.clock();
    require(board.m68k().resetComplete() && board.m68k().state().cycles == 40);
    require(board.m68k().state().address[7] == 0x00ffff00);
    require(board.m68k().state().pc == 8);

    require(board.m68k().state().prefetch[0] == 0x4e71);
    for (unsigned i = 0; i < 28; ++i)
        board.clock();
    require(board.m68k().state().pc == 10 && board.m68k().state().instructions == 1);

    for (unsigned i = 0; i < 12 * Scheduler::M68KDivider; ++i)
        board.clock();
    require(board.m68k().state().pc == 12 && board.m68k().state().instructions == 2);
    require(board.readWord(0) == 0x00ff && board.m68k().lastException().serial == 0);

    board.writeByte(0xff0000, 0x5a);
    board.writeByte(0xa00000, 0x5a);
    board.reset();
    require(board.masterCycles() == 0 && !board.m68k().resetVectorsLoaded());
    require(board.readWord(0xff0000) == 0 && board.readByte(0xa00000) == 0);
    require(board.vdp().cramWord(0) == 0 && board.ym2612().registerValue(0, 0x22) == 0);
    for (unsigned i = 0; i < 40 * Scheduler::M68KDivider; ++i)
        board.clock();
    require(board.m68k().resetComplete() && board.m68k().state().pc == 8 &&
            board.m68k().state().prefetch[0] == 0x4e71);

    board.writeWord(0xff0000, 0x1234);
    require(board.readByte(0xe00000) == 0x12 && board.readByte(0xffffff) == 0);
    require(board.readWord(0xe10000) == 0x1234);
    board.writeByte(0xffffff, 0x56);
    require(board.readByte(0xefffff) == 0x56);
    bool rejected = false;
    try
    {
        board.readWord(0xff0001);
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    require(rejected);
    rejected = false;
    try
    {
        board.readByte(0x400000);
    }
    catch (const std::logic_error &)
    {
        rejected = true;
    }
    require(rejected);

    const auto memoryPath = directory / "board-memory.bin";
    const std::vector<unsigned char> memoryRom{
        0, 0, 0, 0, 0, 0, 0, 8,
        0x41, 0xf8, 0xff, 0x00, // LEA $FF00.W,A0
        0x70, 0x34,             // MOVEQ #$34,D0
        0x30, 0x80,             // MOVE.W D0,(A0)
        0x32, 0x10,             // MOVE.W (A0),D1
        0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71};
    {
        std::ofstream file(memoryPath, std::ios::binary);
        file.write(reinterpret_cast<const char *>(memoryRom.data()), memoryRom.size());
        require(bool(file));
    }
    auto memoryBoardStorage = std::make_unique<Board>();
    Board &memoryBoard = *memoryBoardStorage;
    memoryBoard.insertCartridge(Cartridge::load(memoryPath));
    for (unsigned i = 0; i < 68 * 7; ++i)
        memoryBoard.clock();
    require(memoryBoard.m68k().state().cycles == 68 && memoryBoard.m68k().state().pc == 18);
    require(memoryBoard.readWord(0xe0ff00) == 0x34 && memoryBoard.m68k().state().data[1] == 0x34);

    const auto longPath = directory / "board-long.bin";
    const std::vector<unsigned char> longRom{
        0, 0, 0, 0, 0, 0, 0, 8,
        0x41, 0xf8, 0xff, 0x00, // LEA $FF00.W,A0
        0x70, 0xff, 0x50, 0x80, // MOVEQ #-1,D0; ADDQ.L #8,D0
        0x20, 0x80, 0x22, 0x10, // MOVE.L D0,(A0); MOVE.L (A0),D1
        0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71};
    {
        std::ofstream file(longPath, std::ios::binary);
        file.write(reinterpret_cast<const char *>(longRom.data()), longRom.size());
        require(bool(file));
    }
    auto longBoardStorage = std::make_unique<Board>();
    Board &longBoard = *longBoardStorage;
    longBoard.insertCartridge(Cartridge::load(longPath));
    for (unsigned i = 0; i < 84 * 7; ++i)
        longBoard.clock();
    require(longBoard.m68k().state().pc == 20 && longBoard.m68k().state().data[1] == 7);
    require(longBoard.readWord(0xe0ff00) == 0 && longBoard.readWord(0xe0ff02) == 7);

    const auto interruptPath = directory / "board-interrupt.bin";
    std::vector<unsigned char> interruptRom(0x100);
    const auto putWord = [&](unsigned address, std::uint16_t value)
    {
        interruptRom[address] = static_cast<unsigned char>(value >> 8);
        interruptRom[address + 1] = static_cast<unsigned char>(value);
    };
    putWord(0, 0x00ff);
    putWord(2, 0xff00); // SSP na Work RAM.
    putWord(4, 0);
    putWord(6, 8); // PC inicial.
    putWord(8, 0x4e73);
    putWord(10, 0x4e71); // RTE prepara SR com máscara zero.
    putWord(0x20, 0x4e71);
    putWord(0x22, 0x4e71);
    putWord(0x24, 0x4e71);
    putWord(0x30, 0x4e73); // Tratador: RTE.
    putWord(28 * 4, 0);
    putWord(28 * 4 + 2, 0x30); // Autovetor de nível 4.
    {
        std::ofstream file(interruptPath, std::ios::binary);
        file.write(reinterpret_cast<const char *>(interruptRom.data()), interruptRom.size());
        require(bool(file));
    }
    auto interruptBoardStorage = std::make_unique<Board>();
    Board &interruptBoard = *interruptBoardStorage;
    interruptBoard.insertCartridge(Cartridge::load(interruptPath));
    interruptBoard.writeWord(0xffff00, 0x2000);
    interruptBoard.writeWord(0xffff02, 0);
    interruptBoard.writeWord(0xffff04, 0x20);
    for (unsigned i = 0; i < 60 * 7; ++i)
        interruptBoard.clock();
    require(interruptBoard.m68k().state().pc == 0x20);
    interruptBoard.setM68KInterruptLevel(4);
    for (unsigned i = 0; i < 44 * 7; ++i)
        interruptBoard.clock();
    require(interruptBoard.m68k().state().pc == 0x30 &&
            interruptBoard.m68k().state().sr == 0x2400);
    require(interruptBoard.readWord(0xffff00) == 0x2000 &&
            interruptBoard.readWord(0xffff04) == 0x20);
    interruptBoard.setM68KInterruptLevel(0);
    for (unsigned i = 0; i < 20 * 7; ++i)
        interruptBoard.clock();
    require(interruptBoard.m68k().state().pc == 0x20 &&
            interruptBoard.m68k().state().sr == 0x2000);

    const auto verticalPath = directory / "board-vertical-interrupt.bin";
    std::vector<unsigned char> verticalRom(0x100);
    const auto putVerticalWord = [&](unsigned address, std::uint16_t value)
    {
        verticalRom[address] = static_cast<unsigned char>(value >> 8);
        verticalRom[address + 1] = static_cast<unsigned char>(value);
    };
    putVerticalWord(0, 0x00ff);
    putVerticalWord(2, 0xff00); // SSP na Work RAM.
    putVerticalWord(6, 8);      // O RTE inicial restaura SR e PC da pilha.
    putVerticalWord(8, 0x4e73);
    putVerticalWord(0x20, 0x60fe); // Aguarda a interrupção em um laço.
    putVerticalWord(0x40, 0x7001); // Tratador vertical: MOVEQ #1,D0.
    putVerticalWord(0x42, 0x4e73);
    putVerticalWord(30 * 4, 0);
    putVerticalWord(30 * 4 + 2, 0x40); // Autovetor de nível 6.
    {
        std::ofstream file(verticalPath, std::ios::binary);
        file.write(reinterpret_cast<const char *>(verticalRom.data()), verticalRom.size());
        require(bool(file));
    }
    auto verticalBoard = std::make_unique<Board>();
    verticalBoard->insertCartridge(Cartridge::load(verticalPath));
    verticalBoard->writeWord(0xffff00, 0x2000);
    verticalBoard->writeWord(0xffff02, 0);
    verticalBoard->writeWord(0xffff04, 0x20);
    verticalBoard->writeWord(0xc00004, 0x8120);
    const auto deadline = VDP::visibleLines * VDP::masterCyclesPerLine + 2000;
    while (verticalBoard->masterCycles() < deadline && verticalBoard->m68k().state().data[0] != 1)
        verticalBoard->clock();
    require(verticalBoard->m68k().state().data[0] == 1 && verticalBoard->vdp().interruptLevel() == 0);

    const auto dmaPath = directory / "board-dma.bin";
    std::vector<unsigned char> dmaRom(0x100);
    dmaRom[3] = 0x80;
    dmaRom[7] = 8;
    dmaRom[8] = 0x60;
    dmaRom[9] = 0xfe; // BRA para si mesmo.
    dmaRom[0x40] = 0x12;
    dmaRom[0x41] = 0x34;
    dmaRom[0x42] = 0x56;
    dmaRom[0x43] = 0x78;
    dmaRom[0x44] = 0x00;
    dmaRom[0x45] = 0x0e; // Vermelho na entrada zero da CRAM.
    dmaRom[0x46] = 0x00;
    dmaRom[0x47] = 0xe0; // Verde.
    dmaRom[0x48] = 0x0e;
    dmaRom[0x49] = 0x00; // Azul.
    {
        std::ofstream file(dmaPath, std::ios::binary);
        file.write(reinterpret_cast<const char *>(dmaRom.data()), dmaRom.size());
        require(bool(file));
    }
    auto dmaBoardStorage = std::make_unique<Board>();
    Board &dmaBoard = *dmaBoardStorage;
    dmaBoard.insertCartridge(Cartridge::load(dmaPath));
    dmaBoard.writeWord(0xc00004, 0x8110); // Habilita DMA.
    dmaBoard.writeWord(0xc00004, 0x8f02); // Incremento de dois bytes.
    dmaBoard.writeWord(0xc00004, 0x9302); // Duas palavras.
    dmaBoard.writeWord(0xc00004, 0x9400);
    dmaBoard.writeWord(0xc00004, 0x9520); // Origem: $000040.
    dmaBoard.writeWord(0xc00004, 0x9600);
    dmaBoard.writeWord(0xc00004, 0x9700);
    dmaBoard.writeWord(0xc00004, 0x4000); // Destino: VRAM $0000.
    dmaBoard.writeWord(0xc00006, 0x0080);
    require(dmaBoard.vdp().dmaBlocksM68K());
    for (unsigned i = 0; i < 100 && dmaBoard.vdp().dmaActive(); ++i)
    {
        const auto cpuCycles = dmaBoard.m68k().state().cycles;
        dmaBoard.clock();
        if (dmaBoard.vdp().dmaActive())
            require(dmaBoard.m68k().state().cycles == cpuCycles);
    }
    require(!dmaBoard.vdp().dmaActive());
    require(dmaBoard.vdp().vramByte(0) == 0x12 && dmaBoard.vdp().vramByte(1) == 0x34);
    require(dmaBoard.vdp().vramByte(2) == 0x56 && dmaBoard.vdp().vramByte(3) == 0x78);
    dmaBoard.writeWord(0xc00004, 0x4000); // Limpa o padrão zero antes do teste de fundo.
    dmaBoard.writeWord(0xc00004, 0x0000);
    dmaBoard.writeWord(0xc00000, 0);
    dmaBoard.writeWord(0xc00000, 0);
    dmaBoard.writeWord(0xc00004, 0x8150); // DMA e exibição habilitadas.
    dmaBoard.writeWord(0xc00004, 0x9301);
    dmaBoard.writeWord(0xc00004, 0x9400);
    dmaBoard.writeWord(0xc00004, 0x9522); // Origem: cor em $000044.
    dmaBoard.writeWord(0xc00004, 0x9600);
    dmaBoard.writeWord(0xc00004, 0x9700);
    dmaBoard.writeWord(0xc00004, 0xc000); // Destino: CRAM, entrada zero.
    dmaBoard.writeWord(0xc00006, 0x0080);
    while (dmaBoard.vdp().frame() == 0)
        dmaBoard.clock();
    require(dmaBoard.vdp().cramWord(0) == 0x000e);
    require(dmaBoard.vdp().pixels().front() == 0xfffc0000);
    require(dmaBoard.vdp().pixels()[(VDP::height - 1) * VDP::width + VDP::h32Width - 1] == 0xfffc0000);
    const auto dmaColor = [&](std::uint8_t sourceWord)
    {
        dmaBoard.writeWord(0xc00004, 0x9301);
        dmaBoard.writeWord(0xc00004, 0x9400);
        dmaBoard.writeWord(0xc00004, 0x9500 | sourceWord);
        dmaBoard.writeWord(0xc00004, 0x9600);
        dmaBoard.writeWord(0xc00004, 0x9700);
        dmaBoard.writeWord(0xc00004, 0xc000);
        dmaBoard.writeWord(0xc00006, 0x0080);
    };
    while (dmaBoard.vdp().line() < 74)
        dmaBoard.clock();
    dmaColor(0x23); // Verde a partir da linha 74.
    while (dmaBoard.vdp().line() < 148)
        dmaBoard.clock();
    dmaColor(0x24); // Azul a partir da linha 148.
    while (dmaBoard.vdp().frame() == 1)
        dmaBoard.clock();
    require(dmaBoard.vdp().pixels()[0] == 0xfffc0000);
    require(dmaBoard.vdp().pixels()[100 * VDP::width] == 0xff00fc00);
    require(dmaBoard.vdp().pixels()[200 * VDP::width] == 0xff0000fc);
    auto z80Board = std::make_unique<Board>();
    z80Board->insertCartridge(Cartridge::load(dmaPath));
    require(z80Board->readWord(0xa11100) == 0 && z80Board->z80().state().cycles == 0);
    z80Board->writeWord(0xa00000, 0x7600); // A palavra escreve o byte alto na RAM do Z80.
    require(z80Board->readMemory(0) == 0x76 && z80Board->readWord(0xa00000) == 0x7676);
    z80Board->writeByte(0xa11200, 1); // Libera RESET; o Z80 passa a executar.
    for (unsigned i = 0; i < 15; ++i)
        z80Board->clock();
    require(z80Board->z80().state().cycles == 1 && z80Board->readWord(0xa11100) == 0x0100);
    z80Board->writeWord(0xa11100, 0x0100);
    require(z80Board->readWord(0xa11100) == 0x0100); // Ainda termina o M-cycle atual.
    for (unsigned i = 0; i < 60; ++i)
        z80Board->clock();
    require(z80Board->readWord(0xa11100) == 0);
    z80Board->writeByte(0xa00001, 0x12);
    require(z80Board->readMemory(1) == 0x12);
    z80Board->writeWord(0xa11100, 0);
    require(z80Board->readWord(0xa11100) == 0x0100);
    bool denied = false;
    try
    {
        z80Board->readByte(0xa00000);
    }
    catch (const std::logic_error &)
    {
        denied = true;
    }
    require(denied);
    z80Board->writeMemory(0x5000, 0x22);
    z80Board->writeMemory(0x5001, 0x08);
    require(z80Board->readMemory(0x5000) == 0x80 && z80Board->ym2612().registerValue(0, 0x22) == 8);
    z80Board->writeWord(0xa11200, 0); // RESET volta a dar acesso imediato ao 68000.
    require(z80Board->readWord(0xa11100) == 0 && z80Board->z80().state().cycles == 0);
    require(z80Board->readMemory(0x4000) == 0 && z80Board->ym2612().registerValue(0, 0x22) == 0);
    z80Board->writeByte(0xc00011, 0x82); // O 68000 escreve o PSG pelo byte ímpar.
    z80Board->writeByte(0xc00013, 0x00);
    z80Board->writeWord(0xc00010, 0x0090); // A palavra usa o byte baixo.
    require(z80Board->psg().registerValue(0) == 2);
    require(z80Board->psg().registerValue(1) == 0);
    z80Board->writeMemory(0x7f11, 0x9f); // O Z80 também alcança o PSG.
    require(z80Board->psg().registerValue(1) == 15);
    z80Board->insertCartridge(Cartridge::load(dmaPath));
    z80Board->writeByte(0xc00011, 0x82);
    z80Board->writeByte(0xc00011, 0x00);
    z80Board->writeByte(0xc00011, 0x90);
    require(z80Board->psg().sample() < 0);
    for (unsigned i = 0; i < 240; ++i)
        z80Board->clock(); // O divisor mestre/15 fornece 16 entradas ao PSG.
    require(z80Board->psg().sample() > 0);
    require(z80Board->readMemory(0x8000) == dmaRom[0]); // Banco inicial: ROM na origem.
    for (unsigned bit = 0; bit < 9; ++bit)
        z80Board->writeMemory(0x6000, std::uint8_t((0x1fe >> bit) & 1));
    z80Board->writeByte(0xff0000, 0x5a);
    require(z80Board->readMemory(0x8000) == 0x5a); // Banco $FF0000: Work RAM.
    z80Board->writeMemory(0x8001, 0x3c);
    require(z80Board->readByte(0xff0001) == 0x3c);
    z80Board->insertCartridge(Cartridge::load(dmaPath));
    require(z80Board->readMemory(0x8000) == dmaRom[0]); // Inserir cartucho reinicia o banco.
    for (unsigned bit = 0; bit < 9; ++bit)
        z80Board->writeWord(0xa06000, std::uint16_t(((0x1fe >> bit) & 1) << 8));
    z80Board->writeByte(0xff0000, 0x76);
    require(z80Board->readMemory(0x8000) == 0x76); // O 68000 também programa o banco.
    const auto framePath = directory / "vdp-dma-frame.ppm";
    {
        std::ofstream frame(framePath, std::ios::binary);
        frame << "P6\n"
              << VDP::width << ' ' << VDP::height << "\n255\n";
        for (const auto pixel : dmaBoard.vdp().pixels())
        {
            const char rgb[3]{char(pixel >> 16), char(pixel >> 8), char(pixel)};
            frame.write(rgb, 3);
        }
        require(bool(frame));
    }
    return 0;
}
#include "Core/Board.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace R2MEGA::Core;

static void require(bool value)
{
    if (!value)
        throw std::runtime_error("Falha no teste da placa");
}

int main(int argc, char **argv)
{
    require(argc == 2);
    const auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    const auto path = directory / "board-reset.bin";
    const std::vector<unsigned char> rom{0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x08,
                                         0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71};
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(rom.data()), rom.size());
        require(bool(file));
    }

    Board board;
    require(board.masterCycles() == 0 && !board.m68k().resetVectorsLoaded());
    for (unsigned i = 0; i < 14; ++i)
        board.clock();
    require(board.masterCycles() == 14 && board.m68k().state().cycles == 2);
    board.insertCartridge(Cartridge::load(path));
    require(board.masterCycles() == 0 && !board.m68k().resetVectorsLoaded());
    require(board.readByte(0) == 0 && board.readWord(0) == 0x00ff);
    require(board.readWord(2) == 0xff00);
    for (unsigned i = 0; i < 279; ++i)
        board.clock();
    require(board.m68k().resetVectorsLoaded() && !board.m68k().resetComplete() && board.m68k().state().cycles == 39);
    board.clock();
    require(board.m68k().resetComplete() && board.m68k().state().cycles == 40);
    require(board.m68k().state().address[7] == 0x00ffff00);
    require(board.m68k().state().pc == 8);

    require(board.m68k().state().prefetch[0] == 0x4e71);
    for (unsigned i = 0; i < 28; ++i) board.clock();
    require(board.m68k().state().pc == 10 && board.m68k().state().instructions == 1);

    board.writeWord(0xff0000, 0x1234);
    require(board.readByte(0xe00000) == 0x12 && board.readByte(0xffffff) == 0);
    require(board.readWord(0xe10000) == 0x1234);
    board.writeByte(0xffffff, 0x56);
    require(board.readByte(0xefffff) == 0x56);
    bool rejected = false;
    try { board.readWord(0xff0001); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected);
    rejected = false;
    try { board.readByte(0x400000); }
    catch (const std::logic_error &) { rejected = true; }
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
    Board memoryBoard;
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
    Board longBoard;
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
    putWord(0, 0x00ff); putWord(2, 0xff00); // SSP na Work RAM.
    putWord(4, 0); putWord(6, 8); // PC inicial.
    putWord(8, 0x4e73); putWord(10, 0x4e71); // RTE prepara SR com máscara zero.
    putWord(0x20, 0x4e71); putWord(0x22, 0x4e71);
    putWord(0x24, 0x4e71); putWord(0x30, 0x4e73); // Tratador: RTE.
    putWord(28 * 4, 0); putWord(28 * 4 + 2, 0x30); // Autovetor de nível 4.
    {
        std::ofstream file(interruptPath, std::ios::binary);
        file.write(reinterpret_cast<const char *>(interruptRom.data()), interruptRom.size());
        require(bool(file));
    }
    Board interruptBoard;
    interruptBoard.insertCartridge(Cartridge::load(interruptPath));
    interruptBoard.writeWord(0xffff00, 0x2000);
    interruptBoard.writeWord(0xffff02, 0);
    interruptBoard.writeWord(0xffff04, 0x20);
    for (unsigned i = 0; i < 60 * 7; ++i) interruptBoard.clock();
    require(interruptBoard.m68k().state().pc == 0x20);
    interruptBoard.setM68KInterruptLevel(4);
    for (unsigned i = 0; i < 44 * 7; ++i) interruptBoard.clock();
    require(interruptBoard.m68k().state().pc == 0x30 &&
            interruptBoard.m68k().state().sr == 0x2400);
    require(interruptBoard.readWord(0xffff00) == 0x2000 &&
            interruptBoard.readWord(0xffff04) == 0x20);
    interruptBoard.setM68KInterruptLevel(0);
    for (unsigned i = 0; i < 20 * 7; ++i) interruptBoard.clock();
    require(interruptBoard.m68k().state().pc == 0x20 &&
            interruptBoard.m68k().state().sr == 0x2000);
    return 0;
}

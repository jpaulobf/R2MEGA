#include "Core/Board.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void putWord(std::vector<std::uint8_t> &rom, unsigned address, std::uint16_t value)
    {
        rom[address] = static_cast<std::uint8_t>(value >> 8);
        rom[address + 1] = static_cast<std::uint8_t>(value);
    }

    void runUntil(Board &board, std::uint64_t instructions)
    {
        const auto deadline = board.masterCycles() + 4000;
        while (board.m68k().state().instructions < instructions &&
               board.masterCycles() < deadline)
            board.clock();
        check(board.m68k().state().instructions == instructions,
              "A ROM de diagnóstico não concluiu as instruções esperadas");
        check(board.masterCycles() == board.m68k().state().cycles * 7,
              "O scheduler perdeu a proporção de sete clocks mestres por ciclo do 68000");
    }
}

int main(int argc, char **argv)
{
    try
    {
        check(argc == 2, "Diretório de saída da ROM ausente");
        const auto directory = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(directory);
        const auto path = directory / "m68k-diagnostic.md";
        std::vector<std::uint8_t> rom(0x200, 0);
        putWord(rom, 0x000, 0x00ff); // SSP = $00FFFF00, dentro da Work RAM.
        putWord(rom, 0x002, 0xff00);
        putWord(rom, 0x004, 0x0000); // PC inicial = $00000100.
        putWord(rom, 0x006, 0x0100);
        putWord(rom, 0x100, 0x41f8); // LEA $FF20.W,A0
        putWord(rom, 0x102, 0xff20);
        putWord(rom, 0x104, 0x7006); // MOVEQ #6,D0
        putWord(rom, 0x106, 0x7207); // MOVEQ #7,D1
        putWord(rom, 0x108, 0xc0c1); // MULU.W D1,D0: 42
        putWord(rom, 0x10a, 0x30c0); // MOVE.W D0,(A0)+
        putWord(rom, 0x10c, 0x7202); // MOVEQ #2,D1
        putWord(rom, 0x10e, 0x80c1); // DIVU.W D1,D0: 21
        putWord(rom, 0x110, 0x30c0); // MOVE.W D0,(A0)+
        putWord(rom, 0x112, 0x74fd); // MOVEQ #-3,D2
        putWord(rom, 0x114, 0xc1c2); // MULS.W D2,D0: -63
        putWord(rom, 0x116, 0x2080); // MOVE.L D0,(A0)
        putWord(rom, 0x118, 0x0c80); // CMPI.L #-63,D0
        putWord(rom, 0x11a, 0xffff);
        putWord(rom, 0x11c, 0xffc1);
        putWord(rom, 0x11e, 0x6604); // BNE.S para o caminho de falha.
        putWord(rom, 0x120, 0x7601); // Sucesso: D3 = 1.
        putWord(rom, 0x122, 0x6002); // Salta o caminho de falha.
        putWord(rom, 0x124, 0x7600); // Falha: D3 = 0.
        putWord(rom, 0x126, 0x60fe); // Laço final.
        putWord(rom, 0x128, 0x4e71);
        {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()),
                       static_cast<std::streamsize>(rom.size()));
            check(bool(file), "Falha ao gravar a ROM de diagnóstico");
        }

        Board board;
        board.insertCartridge(Cartridge::load(path));
        runUntil(board, 5);
        check(board.readWord(0xffff20) == 42 && board.m68k().state().address[0] == 0xffffff22,
              "MULU e primeira escrita na Work RAM falharam");
        runUntil(board, 8);
        check(board.readWord(0xffff22) == 21 && board.m68k().state().data[0] == 21,
              "DIVU e segunda escrita na Work RAM falharam");
        runUntil(board, 11);
        check(board.m68k().state().pc == 0x118 &&
                  board.m68k().state().data[0] == 0xffffffc1u &&
                  board.readWord(0xffff24) == 0xffff &&
                  board.readWord(0xffff26) == 0xffc1,
              "MULS e escrita longa na Work RAM falharam");
        runUntil(board, 12);
        check((board.m68k().state().sr & 0x04) != 0 &&
                  board.m68k().state().data[0] == 0xffffffc1u,
              "CMPI.L alterou D0 ou não marcou a igualdade");
        runUntil(board, 16);
        check(board.m68k().state().pc == 0x126 && board.m68k().state().data[3] == 1 &&
                  board.readWord(0xffff20) == 42 && board.readWord(0xffff22) == 21 &&
                  board.readWord(0xffff26) == 0xffc1,
              "BNE não preservou o caminho de sucesso da ROM");
        putWord(rom, 0x11c, 0xffc2); // Uma comparação diferente deve tomar BNE.
        const auto mismatchPath = directory / "m68k-diagnostic-mismatch.md";
        {
            std::ofstream file(mismatchPath, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()),
                       static_cast<std::streamsize>(rom.size()));
            check(bool(file), "Falha ao gravar a variante da ROM");
        }
        Board mismatchBoard;
        mismatchBoard.insertCartridge(Cartridge::load(mismatchPath));
        runUntil(mismatchBoard, 12);
        check((mismatchBoard.m68k().state().sr & 0x04) == 0,
              "CMPI.L marcou igualdade para valores diferentes");
        runUntil(mismatchBoard, 13);
        check(mismatchBoard.m68k().state().pc == 0x124,
              "BNE não desviou para o caminho de falha da ROM");
        runUntil(mismatchBoard, 14);
        check(mismatchBoard.m68k().state().pc == 0x126 &&
                  mismatchBoard.m68k().state().data[3] == 0,
              "O caminho de falha da ROM não concluiu com D3 zerado");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
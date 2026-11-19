#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    struct Bus : M68KBus
    {
        std::unordered_map<std::uint32_t, std::uint16_t> words;
        std::vector<std::pair<char, std::uint32_t>> transfers;
        std::vector<std::pair<std::uint32_t, unsigned>> readCodes;
        std::uint32_t waitAddress = 0xffffffffu;
        unsigned writeWaits = 0;

        std::uint16_t readWord(std::uint32_t address) override
        {
            transfers.emplace_back('R', address);
            const auto found = words.find(address);
            return found == words.end() ? 0x4e71 : found->second;
        }
        std::uint8_t readByte(std::uint32_t address) override
        {
            const std::uint16_t word = words[address & ~1u];
            return static_cast<std::uint8_t>(address & 1 ? word : word >> 8);
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            transfers.emplace_back('W', address);
            words[address] = value;
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            const std::uint32_t aligned = address & ~1u;
            const std::uint16_t previous = words[aligned];
            words[aligned] = address & 1 ? static_cast<std::uint16_t>((previous & 0xff00) | value) : static_cast<std::uint16_t>((previous & 0x00ff) | (value << 8));
        }
        bool tryWriteWord(std::uint32_t address, std::uint16_t value) override
        {
            if (address == waitAddress && writeWaits)
            {
                --writeWaits;
                return false;
            }
            writeWord(address, value);
            return true;
        }
        void observe(const M68KPins &pins) override
        {
            if (pins.active && !pins.write && pins.phase == 2 && !pins.waiting)
                readCodes.emplace_back(pins.address, pins.functionCode);
        }
        void setup()
        {
            words[0] = 0;
            words[2] = 0x1800;
            words[4] = 0;
            words[6] = 0x100;
            words[16] = 0;
            words[18] = 0x240;
            words[32] = 0;
            words[34] = 0x200;
            words[36] = 0;
            words[38] = 0x280;
            for (unsigned address = 0x100; address < 0x130; address += 2)
                words[address] = 0x4e71;
            for (unsigned address : {0x200u, 0x202u, 0x240u,
                                     0x242u, 0x280u, 0x282u})
                words[address] = 0x4e71;
        }
    };

    void check(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    void boot(Moto68000 &cpu)
    {
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        check(cpu.resetComplete(), "reset incompleto");
    }
    unsigned execute(Moto68000 &cpu)
    {
        const auto previous = cpu.state().instructions;
        unsigned cycles = 0;
        while (cpu.state().instructions == previous && cycles < 100)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == previous + 1,
              "instrução não terminou");
        return cycles;
    }
    void runUntilPc(Moto68000 &cpu, std::uint32_t pc)
    {
        unsigned cycles = 0;
        while (cpu.state().pc != pc && cycles++ < 100)
            cpu.clock();
        check(cpu.state().pc == pc, "vetor de exceção não carregado");
    }

    void registerAndImmediate()
    {
        Bus from;
        from.setup();
        from.words[0x100] = 0x203c; // MOVE.L #$12345678,D0.
        from.words[0x102] = 0x1234;
        from.words[0x104] = 0x5678;
        from.words[0x106] = 0x7480;
        from.words[0x108] = 0x4402; // X,N,V,C = 1.
        from.words[0x10a] = 0x40c0; // MOVE SR,D0.
        Moto68000 fromCpu(from);
        boot(fromCpu);
        execute(fromCpu);
        execute(fromCpu);
        execute(fromCpu);
        from.transfers.clear();
        check(execute(fromCpu) == 6 &&
                  fromCpu.state().data[0] == 0x1234271b &&
                  fromCpu.state().sr == 0x271b &&
                  from.transfers ==
                      std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10e}}),
              "MOVE SR,D0 preserva metade alta e gasta seis ciclos");

        Bus toCcr;
        toCcr.setup();
        toCcr.words[0x100] = 0x701b; // MOVEQ #$1b,D0.
        toCcr.words[0x102] = 0x44c0; // MOVE D0,CCR.
        Moto68000 toCcrCpu(toCcr);
        boot(toCcrCpu);
        execute(toCcrCpu);
        toCcr.transfers.clear();
        check(execute(toCcrCpu) == 12 && toCcrCpu.state().sr == 0x271b &&
                  toCcr.transfers ==
                      std::vector<std::pair<char, std::uint32_t>>({{'R', 0x104}, {'R', 0x106}}),
              "MOVE D0,CCR altera somente CCR e refaz a pré-busca");

        Bus immediate;
        immediate.setup();
        immediate.words[0x100] = 0x44fc; // MOVE #$ffff,CCR.
        immediate.words[0x102] = 0xffff;
        Moto68000 immediateCpu(immediate);
        boot(immediateCpu);
        immediate.transfers.clear();
        check(execute(immediateCpu) == 16 && immediateCpu.state().sr == 0x271f &&
                  immediate.transfers ==
                      std::vector<std::pair<char, std::uint32_t>>({{'R', 0x104}, {'R', 0x104}, {'R', 0x106}}),
              "MOVE imediato para CCR ignora os bits reservados");

        Bus toSr;
        toSr.setup();
        toSr.words[0x100] = 0x46fc; // MOVE #$0700,SR remove S.
        toSr.words[0x102] = 0x0700;
        Moto68000 toSrCpu(toSr);
        boot(toSrCpu);
        toSr.readCodes.clear();
        check(execute(toSrCpu) == 16 && toSrCpu.state().sr == 0x0700 &&
                  toSrCpu.state().address[7] == 0 &&
                  toSr.readCodes ==
                      std::vector<std::pair<std::uint32_t, unsigned>>({{0x104, 6}, {0x104, 2}, {0x106, 2}}),
              "MOVE imediato para SR troca de pilha e usa pré-busca de usuário");
    }

    void memoryModes()
    {
        struct Mode
        {
            unsigned mode, reg, extra;
            std::uint16_t extension, base;
        };
        constexpr std::array<Mode, 7> modes{{{2, 0, 0, 0, 0x1100}, {3, 0, 0, 0, 0x1100}, {4, 0, 2, 0, 0x1102}, {5, 0, 4, 2, 0x10fe}, {6, 0, 6, 0x1002, 0x10fe}, {7, 0, 4, 0x1100, 0x1100}, {7, 1, 8, 0, 0x1100}}};
        for (const auto &mode : modes)
            for (unsigned kind : {0u, 1u, 2u})
            {
                Bus bus;
                bus.setup();
                bus.words[0x100] = 0x41f8; // LEA base.W,A0.
                bus.words[0x102] = mode.base;
                bus.words[0x104] = 0x7480;
                bus.words[0x106] = 0x4402; // X,N,V,C = 1.
                bus.words[0x108] = static_cast<std::uint16_t>(
                    (kind == 0 ? 0x40c0 : kind == 1 ? 0x44c0
                                                    : 0x46c0) |
                    (mode.mode << 3) | mode.reg);
                const unsigned extensions = mode.mode == 7 && mode.reg == 1 ? 2 : mode.mode >= 5 ? 1
                                                                                                 : 0;
                if (extensions)
                {
                    bus.words[0x10a] = mode.extension;
                    if (extensions == 2)
                        bus.words[0x10c] = 0x1100;
                }
                bus.words[0x1100] = kind == 0 ? 0xdead : kind == 1 ? 0x0015
                                                                   : 0x2704;
                Moto68000 cpu(bus);
                boot(cpu);
                for (unsigned i = 0; i < 3; ++i)
                    execute(cpu);
                bus.transfers.clear();
                const std::string label = "MOVE SR/CCR modo " +
                                          std::to_string(mode.mode) + "/" +
                                          std::to_string(mode.reg) + " tipo " +
                                          std::to_string(kind);
                const unsigned cycles = (kind == 0 ? 12u : 16u) +
                                        mode.extra;
                check(execute(cpu) == cycles &&
                          cpu.state().sr == (kind == 0 ? 0x271b : kind == 1 ? 0x2715
                                                                            : 0x2704) &&
                          bus.words[0x1100] == (kind == 0 ? 0x271b : kind == 1 ? 0x0015
                                                                               : 0x2704),
                      label + ": ciclos, SR e memória");
                const unsigned expectedA0 = mode.mode == 3 ? 0x1102 : mode.mode == 4 ? 0x1100
                                                                                     : mode.base;
                check(cpu.state().address[0] == expectedA0,
                      label + ": avanço de A0");
                std::vector<std::pair<char, std::uint32_t>> expected;
                for (unsigned i = 0; i < extensions; ++i)
                    expected.emplace_back('R', 0x10c + i * 2);
                expected.emplace_back('R', 0x1100);
                if (kind == 0)
                {
                    expected.emplace_back('R', 0x10c + extensions * 2);
                    expected.emplace_back('W', 0x1100);
                }
                else
                {
                    expected.emplace_back('R', 0x10a + extensions * 2);
                    expected.emplace_back('R', 0x10c + extensions * 2);
                }
                check(bus.transfers == expected,
                      label + ": ordem do barramento");
            }
    }

    void privilegeAndUsp()
    {
        Bus bus;
        bus.setup();
        bus.words[0x100] = 0x41f8;
        bus.words[0x102] = 0x1500;
        bus.words[0x104] = 0x4e60; // MOVE A0,USP.
        bus.words[0x106] = 0x4e69; // MOVE USP,A1.
        bus.words[0x108] = 0x0a7c;
        bus.words[0x10a] = 0x2000; // EORI remove S.
        bus.words[0x10c] = 0x4e69; // MOVE USP,A1 em modo usuário.
        Moto68000 cpu(bus);
        boot(cpu);
        execute(cpu);
        check(execute(cpu) == 4 && cpu.state().address[7] == 0x1800,
              "MOVE A0,USP não altera SSP");
        check(execute(cpu) == 4 && cpu.state().address[1] == 0x1500,
              "MOVE USP,A1 recupera o valor de USP");
        check(execute(cpu) == 20 && cpu.state().address[7] == 0x1500,
              "ao sair do supervisor, A7 recebe USP");
        runUntilPc(cpu, 0x200);
        check(cpu.state().instructions == 4 &&
                  cpu.state().address[7] == 0x17fa &&
                  bus.words[0x17fe] == 0x10c,
              "MOVE USP em modo usuário gera violação de privilégio");

        Bus userRead;
        userRead.setup();
        userRead.words[0x100] = 0x0a7c;
        userRead.words[0x102] = 0x2000; // EORI remove S.
        userRead.words[0x104] = 0x40c0; // MOVE SR,D0 em modo usuário.
        Moto68000 userCpu(userRead);
        boot(userCpu);
        execute(userCpu);
        check(execute(userCpu) == 6 &&
                  userCpu.state().data[0] == 0x0700 &&
                  userCpu.state().pc == 0x106,
              "MOVE SR,D0 é permitido no 68000 em modo usuário");

        Bus userWrite;
        userWrite.setup();
        userWrite.words[0x100] = 0x0a7c;
        userWrite.words[0x102] = 0x2000;
        userWrite.words[0x104] = 0x46fc;
        userWrite.words[0x106] = 0x2700;
        Moto68000 userWriteCpu(userWrite);
        boot(userWriteCpu);
        execute(userWriteCpu);
        runUntilPc(userWriteCpu, 0x200);
        check(userWriteCpu.state().instructions == 1 &&
                  userWrite.words[0x17fe] == 0x104,
              "MOVE para SR de usuário falha antes de ler a extensão");
    }

    void pcRelativeAndWriteWait()
    {
        Bus relative;
        relative.setup();
        relative.words[0x100] = 0x44fa; // MOVE ($0ffe,PC),CCR.
        relative.words[0x102] = 0x0ffe;
        relative.words[0x1100] = 0x0015;
        Moto68000 relativeCpu(relative);
        boot(relativeCpu);
        relative.transfers.clear();
        relative.readCodes.clear();
        check(execute(relativeCpu) == 20 &&
                  relativeCpu.state().sr == 0x2715 &&
                  relative.transfers ==
                      std::vector<std::pair<char, std::uint32_t>>({{'R', 0x104}, {'R', 0x1100}, {'R', 0x104}, {'R', 0x106}}),
              "MOVE relativo ao PC lê o endereço e refaz a pré-busca");
        check(relative.readCodes ==
                  std::vector<std::pair<std::uint32_t, unsigned>>({{0x104, 6}, {0x1100, 6}, {0x104, 6}, {0x106, 6}}),
              "origem relativa ao PC usa o espaço de programa");

        Bus wait;
        wait.setup();
        wait.words[0x100] = 0x41f8;
        wait.words[0x102] = 0x1100;
        wait.words[0x104] = 0x40d0; // MOVE SR,(A0).
        wait.words[0x1100] = 0xdead;
        Moto68000 waitCpu(wait);
        boot(waitCpu);
        execute(waitCpu);
        wait.transfers.clear();
        wait.waitAddress = 0x1100;
        wait.writeWaits = 2;
        check(execute(waitCpu) == 14 && wait.words[0x1100] == 0x2700 &&
                  waitCpu.state().sr == 0x2700,
              "MOVE SR,(A0) espera a confirmação da escrita");
        check(wait.transfers ==
                  std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x108}, {'W', 0x1100}}),
              "espera de MOVE SR não duplica a leitura nem a escrita");

        Bus invalid;
        invalid.setup();
        invalid.words[0x100] = 0x42c0; // MOVE CCR,D0 pertence a CPUs posteriores.
        Moto68000 invalidCpu(invalid);
        boot(invalidCpu);
        runUntilPc(invalidCpu, 0x240);
        check(invalidCpu.state().instructions == 0 &&
                  invalid.words[0x17fe] == 0x100,
              "MOVE CCR,D0 gera instrução ilegal no 68000");
    }
}

int main()
{
    try
    {
        registerAndImmediate();
        memoryModes();
        privilegeAndUsp();
        pcRelativeAndWriteWait();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
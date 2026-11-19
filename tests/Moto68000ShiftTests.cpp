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
        std::vector<std::uint32_t> reads;
        std::vector<std::pair<char, std::uint32_t>> transfers;
        std::uint32_t waitAddress = 0xffffffffu;
        unsigned writeWaits = 0;

        void putLong(std::uint32_t address, std::uint32_t value)
        {
            words[address] = static_cast<std::uint16_t>(value >> 16);
            words[address + 2] = static_cast<std::uint16_t>(value);
        }

        std::uint16_t readWord(std::uint32_t address) override
        {
            reads.push_back(address);
            transfers.emplace_back('R', address);
            const auto found = words.find(address);
            return found == words.end() ? 0x4e71 : found->second;
        }

        std::uint8_t readByte(std::uint32_t address) override
        {
            const auto found = words.find(address & ~1u);
            const std::uint16_t value = found == words.end() ? 0x4e71 : found->second;
            return static_cast<std::uint8_t>(address & 1 ? value : value >> 8);
        }

        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            transfers.emplace_back('W', address);
            words[address] = value;
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

        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            const std::uint32_t aligned = address & ~1u;
            const std::uint16_t previous = words[aligned];
            words[aligned] = address & 1 ? static_cast<std::uint16_t>((previous & 0xff00) | value) : static_cast<std::uint16_t>((previous & 0x00ff) | (value << 8));
        }

        void setup(std::uint16_t opcode, std::uint32_t value,
                   std::uint32_t count, bool initialX)
        {
            putLong(0, 0x1800);
            putLong(4, 0x100);
            words[0x100] = 0x203c; // MOVE.L #valor,D0.
            putLong(0x102, value);
            words[0x106] = 0x223c; // MOVE.L #contagem,D1.
            putLong(0x108, count);
            words[0x10c] = initialX ? 0x7480 : 0x7400;
            words[0x10e] = 0x4402; // NEG.B D2 define X antes do deslocamento.
            words[0x110] = opcode;
            words[0x112] = 0x4e71;
            words[0x114] = 0x4e71;
        }
    };

    void check(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    unsigned execute(Moto68000 &cpu)
    {
        const auto previous = cpu.state().instructions;
        unsigned cycles = 0;
        while (cpu.state().instructions == previous && cycles < 200)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == previous + 1,
              "instrução não terminou");
        return cycles;
    }

    std::uint16_t opcode(unsigned kind, bool left, unsigned size,
                         bool registerCount, unsigned count, unsigned reg = 0)
    {
        return static_cast<std::uint16_t>(0xe000 | (count << 9) |
                                          (left ? 0x0100 : 0) | (size << 6) |
                                          (registerCount ? 0x20 : 0) | (kind << 3) | reg);
    }

    struct Case
    {
        const char *label;
        unsigned kind, size, countField;
        bool left, registerCount, initialX;
        std::uint32_t initial, count, result;
        unsigned flags, cycles;
    };

    void registerShifts()
    {
        constexpr std::array<Case, 20> cases{{{"ASL.B muda o sinal", 0, 0, 1, true, false, true,
                                               0x40, 0, 0x80, 0x0a, 8},
                                              {"ASL.B conserva V entre etapas", 0, 0, 2, true, false, true,
                                               0x40, 0, 0, 0x17, 10},
                                              {"ASR.B replica o sinal", 0, 0, 1, false, false, true,
                                               0x81, 0, 0xc0, 0x19, 8},
                                              {"LSL.W elimina o bit alto", 1, 1, 1, true, false, true,
                                               0x8000, 0, 0, 0x15, 8},
                                              {"LSR.W elimina o bit baixo", 1, 1, 1, false, false, true,
                                               1, 0, 0, 0x15, 8},
                                              {"LSR.W preserva a palavra alta", 1, 1, 1, false, false, true,
                                               0x12348000, 0, 0x12344000, 0, 8},
                                              {"ROL.B não altera X", 3, 0, 1, true, false, true,
                                               0x81, 0, 0x03, 0x11, 8},
                                              {"ROR.B não altera X", 3, 0, 1, false, false, true,
                                               0x81, 0, 0xc0, 0x19, 8},
                                              {"ROXL.B usa X na entrada", 2, 0, 1, true, false, true,
                                               0x80, 0, 1, 0x11, 8},
                                              {"ROXR.B usa X na entrada", 2, 0, 1, false, false, true,
                                               1, 0, 0x80, 0x19, 8},
                                              {"LSR.B com contagem zero", 1, 0, 1, false, true, true,
                                               0x81, 0, 0x81, 0x18, 6},
                                              {"ROXR.B com contagem zero copia X para C", 2, 0, 1, false, true, true,
                                               0x81, 0, 0x81, 0x19, 6},
                                              {"ROR.B com contagem zero limpa C", 3, 0, 1, false, true, true,
                                               0x81, 0, 0x81, 0x18, 6},
                                              {"ROXR.B com X limpo e contagem zero", 2, 0, 1, false, true, false,
                                               0x81, 0, 0x81, 0x08, 6},
                                              {"ASL.B com contagem zero preserva X", 0, 0, 1, true, true, false,
                                               0x80, 0, 0x80, 0x08, 6},
                                              {"D0 também pode fornecer a contagem", 3, 0, 0, true, true, true,
                                               0x81, 0, 0x03, 0x11, 8},
                                              {"contagem de registrador é módulo 64", 1, 0, 1, false, true, true,
                                               0x81, 65, 0x40, 0x11, 8},
                                              {"contagem acima da largura", 1, 0, 1, true, true, true,
                                               0x80, 9, 0, 0x04, 24},
                                              {"ASR.L com 63 deslocamentos", 0, 2, 1, false, true, true,
                                               0x80000000, 63, 0xffffffff, 0x19, 134},
                                              {"LSL.L preserva largura de 32 bits", 1, 2, 1, true, false, true,
                                               0x80000000, 0, 0, 0x15, 10}}};

        for (const auto &test : cases)
        {
            Bus bus;
            bus.setup(opcode(test.kind, test.left, test.size,
                             test.registerCount, test.countField),
                      test.initial, test.count, test.initialX);
            Moto68000 cpu(bus);
            cpu.reset();
            for (unsigned i = 0; i < 40; ++i)
                cpu.clock();
            check(cpu.resetComplete(), "reset incompleto");
            for (unsigned i = 0; i < 4; ++i)
                execute(cpu);
            bus.reads.clear();
            check(execute(cpu) == test.cycles &&
                      cpu.state().data[0] == test.result &&
                      cpu.state().data[1] == test.count &&
                      (cpu.state().sr & 0x1f) == test.flags,
                  std::string(test.label) + ": resultado, flags e ciclos");
            check(bus.reads == std::vector<std::uint32_t>{0x114} &&
                      cpu.state().pc == 0x112,
                  std::string(test.label) + ": pré-busca");
        }
    }

    void memoryShifts()
    {
        struct Mode
        {
            unsigned mode, reg, extra;
            std::uint16_t extension, base;
        };
        constexpr std::array<Mode, 7> modes{{{2, 0, 0, 0, 0x1100}, {3, 0, 0, 0, 0x1100}, {4, 0, 2, 0, 0x1102}, {5, 0, 4, 2, 0x10fe}, {6, 0, 6, 0x1002, 0x10fe}, {7, 0, 4, 0x1100, 0x1100}, {7, 1, 8, 0, 0x1100}}};
        struct Operation
        {
            std::uint16_t initial, result;
            unsigned flags;
        };
        constexpr std::array<Operation, 8> operations{{{0x8001, 0xc000, 0x19}, {0x4000, 0x8000, 0x0a}, {0x0001, 0, 0x15}, {0x8000, 0, 0x15}, {0x0001, 0x8000, 0x19}, {0x8000, 0x0001, 0x11}, {0x0001, 0x8000, 0x19}, {0x8000, 0x0001, 0x11}}};

        for (const auto &mode : modes)
            for (unsigned operation = 0; operation < operations.size(); ++operation)
            {
                Bus bus;
                bus.putLong(0, 0x1800);
                bus.putLong(4, 0x100);
                bus.words[0x100] = 0x41f8; // LEA #base.W,A0.
                bus.words[0x102] = mode.base;
                bus.words[0x104] = 0x7480;
                bus.words[0x106] = 0x4402; // X,N,V,C = 1.
                bus.words[0x108] = static_cast<std::uint16_t>(
                    0xe0c0 | ((operation / 2) << 9) |
                    ((operation & 1) << 8) | (mode.mode << 3) | mode.reg);
                const unsigned extensions = mode.mode == 7 && mode.reg == 1 ? 2 : mode.mode >= 5 ? 1
                                                                                                 : 0;
                if (extensions)
                {
                    bus.words[0x10a] = mode.extension;
                    if (extensions == 2)
                        bus.words[0x10c] = 0x1100;
                }
                bus.words[0x1100] = operations[operation].initial;
                Moto68000 cpu(bus);
                cpu.reset();
                for (unsigned i = 0; i < 40; ++i)
                    cpu.clock();
                check(cpu.resetComplete(), "reset incompleto");
                for (unsigned i = 0; i < 3; ++i)
                    execute(cpu);
                bus.transfers.clear();
                const std::string label = "deslocamento em memória " +
                                          std::to_string(operation) + " modo " +
                                          std::to_string(mode.mode) + "/" +
                                          std::to_string(mode.reg);
                check(execute(cpu) == 12 + mode.extra &&
                          bus.words[0x1100] == operations[operation].result &&
                          (cpu.state().sr & 0x1f) == operations[operation].flags,
                      label + ": resultado, flags e ciclos");
                const unsigned expectedA0 = mode.mode == 3 ? 0x1102 : mode.mode == 4 ? 0x1100
                                                                                     : mode.base;
                check(cpu.state().address[0] == expectedA0,
                      label + ": atualização de A0");
                std::vector<std::pair<char, std::uint32_t>> expected;
                for (unsigned i = 0; i < extensions; ++i)
                    expected.emplace_back('R', 0x10c + i * 2);
                expected.emplace_back('R', 0x1100);
                expected.emplace_back('R', 0x10c + extensions * 2);
                expected.emplace_back('W', 0x1100);
                check(bus.transfers == expected,
                      label + ": ordem do barramento");
            }
    }

    void memoryWriteWait()
    {
        Bus bus;
        bus.putLong(0, 0x1800);
        bus.putLong(4, 0x100);
        bus.words[0x100] = 0x41f8; // LEA $1100.W,A0.
        bus.words[0x102] = 0x1100;
        bus.words[0x104] = 0xe3d0; // LSL.W (A0).
        bus.words[0x1100] = 0x8000;
        Moto68000 cpu(bus);
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        check(cpu.resetComplete(), "reset incompleto");
        execute(cpu);
        bus.transfers.clear();
        bus.waitAddress = 0x1100;
        bus.writeWaits = 2;
        check(execute(cpu) == 14 && bus.words[0x1100] == 0 &&
                  (cpu.state().sr & 0x1f) == 0x15,
              "LSL.W aguarda a escrita antes de atualizar o estado");
        check(bus.transfers ==
                  std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x108}, {'W', 0x1100}}),
              "LSL.W não duplica a escrita após a espera");
    }
}

int main()
{
    try
    {
        registerShifts();
        memoryShifts();
        memoryWriteWait();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
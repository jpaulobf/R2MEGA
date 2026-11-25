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
        std::unordered_map<std::uint32_t, std::uint8_t> bytes;
        std::vector<std::pair<char, std::uint32_t>> transfers;
        std::vector<bool> activeCycles;
        std::uint32_t waitAddress = 0xffffffffu;
        unsigned writeWaits = 0;
        void observe(const M68KPins &pins) override
        {
            activeCycles.push_back(pins.active);
        }
        void putWord(std::uint32_t address, std::uint16_t value)
        {
            bytes[address] = static_cast<std::uint8_t>(value >> 8);
            bytes[address + 1] = static_cast<std::uint8_t>(value);
        }
        std::uint8_t byte(std::uint32_t address) const
        {
            const auto found = bytes.find(address);
            return found == bytes.end() ? 0 : found->second;
        }
        std::uint16_t word(std::uint32_t address) const
        {
            return (std::uint16_t(byte(address)) << 8) | byte(address + 1);
        }
        std::uint8_t readByte(std::uint32_t address) override
        {
            transfers.emplace_back('r', address);
            return byte(address);
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            transfers.emplace_back('R', address);
            return word(address);
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            transfers.emplace_back('w', address);
            bytes[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            transfers.emplace_back('W', address);
            putWord(address, value);
        }
        bool tryWriteByte(std::uint32_t address, std::uint8_t value) override
        {
            if (address == waitAddress && writeWaits)
            {
                --writeWaits;
                return false;
            }
            writeByte(address, value);
            return true;
        }
        void setup()
        {
            putWord(0, 0);
            putWord(2, 0x1800);
            putWord(4, 0);
            putWord(6, 0x100);
            putWord(16, 0);
            putWord(18, 0x200);
            for (unsigned address = 0x100; address < 0x130; address += 2)
                putWord(address, 0x4e71);
            putWord(0x200, 0x4e71);
            putWord(0x202, 0x4e71);
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
        const auto before = cpu.state().instructions;
        unsigned cycles = 0;
        while (cpu.state().instructions == before && cycles < 100)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == before + 1, "instrução não terminou");
        return cycles;
    }
    std::uint16_t opcode(bool immediate, unsigned kind,
                         unsigned mode, unsigned reg)
    {
        return static_cast<std::uint16_t>((immediate ? 0x0800 : 0x0300) |
                                          (kind << 6) | (mode << 3) | reg);
    }
    void registers()
    {
        struct Case
        {
            bool immediate;
            unsigned kind, bit;
            std::uint16_t initial;
            unsigned cycles, flags;
            std::uint32_t result;
        };
        constexpr std::array<Case, 11> cases{{{false, 0, 1, 0x70ff, 6, 0x1b, 0xffffffffu},
                                              {false, 0, 17, 0x7000, 6, 0x1f, 0},
                                              {false, 1, 1, 0x7000, 6, 0x1f, 2},
                                              {false, 1, 17, 0x7000, 8, 0x1f, 0x20000},
                                              {false, 2, 1, 0x70ff, 8, 0x1b, 0xfffffffdu},
                                              {false, 2, 17, 0x70ff, 10, 0x1b, 0xfffdffffu},
                                              {false, 3, 1, 0x7000, 6, 0x1f, 2},
                                              {false, 3, 17, 0x7000, 8, 0x1f, 0x20000},
                                              {true, 0, 31, 0x7000, 10, 0x1f, 0},
                                              {true, 3, 33, 0x7000, 10, 0x1f, 2},
                                              {true, 2, 49, 0x70ff, 14, 0x1b, 0xfffdffffu}}};
        for (const auto &test : cases)
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, test.initial);
            bus.putWord(0x102, static_cast<std::uint16_t>(0x7200 | test.bit));
            bus.putWord(0x104, 0x7480);
            bus.putWord(0x106, 0x4402); // Preserva X,N,V,C no teste do bit.
            bus.putWord(0x108, opcode(test.immediate, test.kind, 0, 0));
            if (test.immediate)
                bus.putWord(0x10a, static_cast<std::uint16_t>(test.bit));
            Moto68000 cpu(bus);
            boot(cpu);
            for (unsigned i = 0; i < 4; ++i)
                execute(cpu);
            check((cpu.state().sr & 0x1f) == 0x1b, "preparo das flags");
            bus.transfers.clear();
            bus.activeCycles.clear();
            const std::string label = "bit em D0 tipo " + std::to_string(test.kind) +
                                      " número " + std::to_string(test.bit);
            check(execute(cpu) == test.cycles && cpu.state().data[0] == test.result &&
                      (cpu.state().sr & 0x1f) == test.flags,
                  label + ": ciclos, dado e flags");
            const std::vector<std::pair<char, std::uint32_t>> expected =
                test.immediate ? std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10c}, {'R', 0x10e}}) : std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10c}});
            check(bus.transfers == expected, label + ": pré-busca");
            std::vector<bool> expectedBus(test.immediate ? 8 : 4, true);
            expectedBus.resize(test.cycles, false);
            check(bus.activeCycles == expectedBus, label + ": pré-busca antes dos ciclos internos");
        }
    }

    void memoryModes()
    {
        struct Mode
        {
            unsigned mode, reg, extra;
            std::uint16_t extension, base;
        };
        constexpr std::array<Mode, 7> modes{{{2, 0, 0, 0, 0x1100}, {3, 0, 0, 0, 0x1100}, {4, 0, 2, 0, 0x1101}, {5, 0, 4, 2, 0x10fe}, {6, 0, 6, 0x1002, 0x10f5}, {7, 0, 4, 0x1100, 0x1100}, {7, 1, 8, 0, 0x1100}}};
        for (const auto &mode : modes)
            for (bool immediate : {false, true})
                for (unsigned kind : {0u, 1u, 2u, 3u})
                {
                    Bus bus;
                    bus.setup();
                    bus.putWord(0x100, 0x41f8);
                    bus.putWord(0x102, mode.base);
                    bus.putWord(0x104, 0x7209); // D1 = 9, bit 1 em memória.
                    bus.putWord(0x106, 0x7480);
                    bus.putWord(0x108, 0x4402); // X,N,V,C=1.
                    bus.putWord(0x10a, opcode(immediate, kind, mode.mode, mode.reg));
                    const unsigned eaExtensions = mode.mode == 7 && mode.reg == 1 ? 2 : mode.mode >= 5 ? 1
                                                                                                       : 0;
                    const unsigned extensions = eaExtensions + (immediate ? 1 : 0);
                    const unsigned eaStart = 0x10c + (immediate ? 2 : 0);
                    if (immediate)
                        bus.putWord(0x10c, 9);
                    if (eaExtensions)
                    {
                        bus.putWord(eaStart, mode.extension);
                        if (eaExtensions == 2)
                            bus.putWord(eaStart + 2, 0x1100);
                    }
                    bus.bytes[0x1100] = kind == 3 ? 0 : 2;
                    Moto68000 cpu(bus);
                    boot(cpu);
                    for (unsigned i = 0; i < 4; ++i)
                        execute(cpu);
                    bus.transfers.clear();
                    bus.activeCycles.clear();
                    const std::string label = "bit memória tipo " +
                                              std::to_string(kind) + " modo " +
                                              std::to_string(mode.mode) + "/" + std::to_string(mode.reg) +
                                              (immediate ? " imediato" : " dinâmico");
                    const unsigned cycles = (kind == 0 ? 8u : 12u) +
                                            (immediate ? 4u : 0u) + mode.extra;
                    check(execute(cpu) == cycles, label + ": ciclos");
                    check(bus.byte(0x1100) == (kind == 1 || kind == 2 ? 0 : 2) &&
                              (cpu.state().sr & 0x1f) == (kind == 3 ? 0x1f : 0x1b),
                          label + ": dado e Z original");
                    const unsigned nextAddress = mode.mode == 3 ? 0x1101 : mode.mode == 4 ? 0x1100
                                                                                          : mode.base;
                    check(cpu.state().address[0] == nextAddress,
                          label + ": avanço de A0");
                    std::vector<std::pair<char, std::uint32_t>> expected;
                    for (unsigned i = 0; i < extensions; ++i)
                        expected.emplace_back('R', 0x10e + i * 2);
                    expected.emplace_back('r', 0x1100);
                    expected.emplace_back('R', 0x10e + extensions * 2);
                    if (kind != 0)
                        expected.emplace_back('w', 0x1100);
                    check(bus.transfers == expected, label + ": ordem do barramento");
                    if (mode.mode == 6)
                    {
                        const unsigned indexIdle = immediate ? 4 : 0;
                        check(!bus.activeCycles.at(indexIdle) && !bus.activeCycles.at(indexIdle + 1) &&
                                  bus.activeCycles.at(indexIdle + 2),
                              label + ": cálculo do índice antes da pré-busca");
                    }
                }
    }

    void specialSources()
    {
        for (bool immediate : {false, true})
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, 0x7201); // D1 = 1.
            bus.putWord(0x102, 0x7480);
            bus.putWord(0x104, 0x4402); // X,N,V,C=1.
            bus.putWord(0x106, opcode(immediate, 0, 7, 2));
            if (immediate)
            {
                bus.putWord(0x108, 1);      // Número do bit.
                bus.putWord(0x10a, 0x0ff6); // PC da extensão $10a até $1100.
            }
            else
                bus.putWord(0x108, 0x0ff8); // PC da extensão $108 até $1100.
            bus.bytes[0x1100] = 2;
            Moto68000 cpu(bus);
            boot(cpu);
            execute(cpu);
            execute(cpu);
            execute(cpu);
            bus.transfers.clear();
            check(execute(cpu) == (immediate ? 16u : 12u) &&
                      (cpu.state().sr & 0x1f) == 0x1b,
                  "BTST relativo ao PC testa o byte no endereço correto");
            const std::vector<std::pair<char, std::uint32_t>> expected = immediate ? std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10a}, {'R', 0x10c}, {'r', 0x1100}, {'R', 0x10e}}) : std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10a}, {'r', 0x1100}, {'R', 0x10c}});
            check(bus.transfers == expected,
                  "BTST relativo ao PC preserva a ordem das leituras");
        }
    }

    void immediateOperand()
    {
        Bus bus;
        bus.setup();
        bus.putWord(0x100, 0x7201); // MOVEQ #1,D1.
        bus.putWord(0x102, 0x033c); // BTST D1,#$02.
        bus.putWord(0x104, 0x0002);
        bus.putWord(0x106, 0x083c); // BTST com dois imediatos, codificação reservada.
        Moto68000 cpu(bus);
        boot(cpu);
        execute(cpu);
        bus.transfers.clear();
        bus.activeCycles.clear();
        check(execute(cpu) == 10 && (cpu.state().sr & 0x04) == 0 &&
                  cpu.state().pc == 0x106,
              "BTST aceita destino imediato com o número do bit em Dn");
        check(bus.transfers ==
                      std::vector<std::pair<char, std::uint32_t>>({{'R', 0x106}, {'R', 0x108}}) &&
                  bus.activeCycles == std::vector<bool>({true, true, true, true, true, true, true, true, false, false}),
              "BTST de destino imediato termina a pré-busca antes dos ciclos internos");
        for (unsigned cycle = 0; cycle < 34; ++cycle)
            cpu.clock();
        check(cpu.lastException().vector == 4 && cpu.lastException().pc == 0x106 &&
                  cpu.lastException().opcode == 0x083c && cpu.state().pc == 0x200,
              "BTST com dois imediatos gera exceção de instrução ilegal");
    }

    void stackAndWait()
    {
        Bus bus;
        bus.setup();
        bus.putWord(0x100, 0x7209);                 // D1 = 9; o bit em memória é 1.
        bus.putWord(0x102, opcode(false, 1, 3, 7)); // BCHG D1,(A7)+.
        bus.bytes[0x1800] = 2;
        Moto68000 cpu(bus);
        boot(cpu);
        execute(cpu);
        bus.transfers.clear();
        bus.waitAddress = 0x1800;
        bus.writeWaits = 2;
        check(execute(cpu) == 14 && cpu.state().address[7] == 0x1802 &&
                  bus.byte(0x1800) == 0 && (cpu.state().sr & 0x04) == 0,
              "BCHG em (A7)+ avança dois bytes e respeita a espera de escrita");
        check(bus.transfers ==
                  std::vector<std::pair<char, std::uint32_t>>({{'r', 0x1800}, {'R', 0x106}, {'w', 0x1800}}),
              "a espera de BCHG não duplica a escrita");
    }
}

int main()
{
    try
    {
        registers();
        memoryModes();
        specialSources();
        immediateOperand();
        stackAndWait();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
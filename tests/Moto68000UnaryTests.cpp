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
        std::uint32_t waitAddress = 0xffffffffu;
        unsigned writeWaits = 0;
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
        void putValue(std::uint32_t address, unsigned size, std::uint32_t value)
        {
            if (size == 1)
                bytes[address] = static_cast<std::uint8_t>(value);
            else
            {
                putWord(address, static_cast<std::uint16_t>(
                                     size == 4 ? value >> 16 : value));
                if (size == 4)
                    putWord(address + 2, static_cast<std::uint16_t>(value));
            }
        }
        std::uint32_t value(std::uint32_t address, unsigned size) const
        {
            return size == 1 ? byte(address) : size == 2 ? word(address)
                                                         : (std::uint32_t(word(address)) << 16) | word(address + 2);
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
        void setup()
        {
            putWord(0, 0);
            putWord(2, 0x1800);
            putWord(4, 0);
            putWord(6, 0x100);
            putWord(16, 0);
            putWord(18, 0x200); // Vetor de instrução ilegal.
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
    std::uint16_t opcode(std::uint16_t base, unsigned size, unsigned mode, unsigned reg)
    {
        const unsigned sizeCode = size == 1 ? 0 : size == 2 ? 1
                                                            : 2;
        return static_cast<std::uint16_t>(base | (sizeCode << 6) | (mode << 3) | reg);
    }

    void registerCases()
    {
        struct Case
        {
            std::uint16_t initial, base;
            unsigned size, flags;
            std::uint32_t result;
        };
        constexpr std::array<Case, 10> cases{{{0x7001, 0x4400, 1, 0x19, 0x000000ffu},
                                              {0x7000, 0x4400, 2, 0x04, 0x00000000u},
                                              {0x70ff, 0x4400, 4, 0x11, 0x00000001u},
                                              {0x7080, 0x4400, 1, 0x1b, 0xffffff80u},
                                              {0x7000, 0x4000, 1, 0x19, 0x000000ffu},
                                              {0x70ff, 0x4000, 1, 0x15, 0xffffff00u},
                                              {0x70ff, 0x4000, 4, 0x15, 0x00000000u},
                                              {0x70ff, 0x4600, 1, 0x14, 0xffffff00u},
                                              {0x7000, 0x4600, 2, 0x18, 0x0000ffffu},
                                              {0x70ff, 0x4600, 4, 0x14, 0x00000000u}}};
        for (const auto &test : cases)
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, test.initial);
            bus.putWord(0x102, 0x74ff); // MOVEQ #-1,D2.
            bus.putWord(0x104, 0x5202); // X=1 e Z=1.
            bus.putWord(0x106, opcode(test.base, test.size, 0, 0));
            Moto68000 cpu(bus);
            boot(cpu);
            execute(cpu);
            execute(cpu);
            execute(cpu);
            bus.transfers.clear();
            const std::string label = "unário em D0 " + std::to_string(test.base) +
                                      " tamanho " + std::to_string(test.size);
            check(execute(cpu) == (test.size == 4 ? 6u : 4u), label + ": ciclos");
            check(cpu.state().data[0] == test.result &&
                      (cpu.state().sr & 0x1f) == test.flags,
                  label + ": resultado e flags");
            check(bus.transfers ==
                      std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10a}}),
                  label + ": pré-busca");
        }

        Bus accumulated;
        accumulated.setup();
        accumulated.putWord(0x100, 0x70ff); // D0.B = $ff.
        accumulated.putWord(0x102, 0x74ff);
        accumulated.putWord(0x104, 0x5202); // X=1, Z=1.
        accumulated.putWord(0x106, 0x7201); // Z=0, X permanece ligado.
        accumulated.putWord(0x108, 0x4000); // NEGX.B D0 resulta em zero.
        Moto68000 accumulatedCpu(accumulated);
        boot(accumulatedCpu);
        for (unsigned i = 0; i < 4; ++i)
            execute(accumulatedCpu);
        check(execute(accumulatedCpu) == 4 &&
                  accumulatedCpu.state().data[0] == 0xffffff00u &&
                  (accumulatedCpu.state().sr & 0x1f) == 0x11,
              "NEGX não restaura Z quando o resultado é zero");

        Bus overflow;
        overflow.setup();
        overflow.putWord(0x100, 0x7080); // D0.B = $80; X inicial é zero.
        overflow.putWord(0x102, 0x4000); // NEGX.B D0.
        Moto68000 overflowCpu(overflow);
        boot(overflowCpu);
        execute(overflowCpu);
        check(execute(overflowCpu) == 4 &&
                  overflowCpu.state().data[0] == 0xffffff80u &&
                  (overflowCpu.state().sr & 0x1f) == 0x1b,
              "NEGX.B detecta overflow sem X de entrada");
    }

    void memoryModes()
    {
        struct Mode
        {
            unsigned mode, reg, extra;
            std::uint16_t extension, base;
        };
        constexpr std::array<Mode, 7> modes{{{2, 0, 0, 0, 0x1100}, {3, 0, 0, 0, 0x1100}, {4, 0, 2, 0, 0x1100}, {5, 0, 4, 2, 0x10fe}, {6, 0, 6, 0x1002, 0x10fc}, {7, 0, 4, 0x1100, 0x1100}, {7, 1, 8, 0, 0x1100}}};
        for (const auto &mode : modes)
            for (std::uint16_t base : {std::uint16_t(0x4400),
                                       std::uint16_t(0x4000),
                                       std::uint16_t(0x4600)})
                for (unsigned size : {1u, 2u, 4u})
                {
                    Bus bus;
                    bus.setup();
                    const std::uint32_t addressBase = mode.mode == 4 ? 0x1100 + size : mode.base;
                    bus.putWord(0x100, 0x7202); // D1.W = 2 para indexação.
                    bus.putWord(0x102, 0x41f8);
                    bus.putWord(0x104, static_cast<std::uint16_t>(addressBase));
                    bus.putWord(0x106, 0x74ff);
                    bus.putWord(0x108, 0x5202); // X=1, Z=1.
                    bus.putWord(0x10a, opcode(base, size, mode.mode, mode.reg));
                    const unsigned extensions = mode.mode == 7 && mode.reg == 1 ? 2 : mode.mode >= 5 ? 1
                                                                                                     : 0;
                    if (extensions)
                    {
                        bus.putWord(0x10c, mode.extension);
                        if (extensions == 2)
                            bus.putWord(0x10e, 0x1100);
                    }
                    const std::uint32_t initial = base == 0x4400 ? 1 : base == 0x4000 ? 0
                                                                   : size == 1        ? 0xff
                                                                   : size == 2        ? 0xffff
                                                                                      : 0xffffffffu;
                    const std::uint32_t expected = base == 0x4600 ? 0 : size == 1 ? 0xff
                                                                    : size == 2   ? 0xffff
                                                                                  : 0xffffffffu;
                    const unsigned flags = base == 0x4600 ? 0x14 : 0x19;
                    bus.putValue(0x1100, size, initial);
                    Moto68000 cpu(bus);
                    boot(cpu);
                    for (unsigned i = 0; i < 4; ++i)
                        execute(cpu);
                    bus.transfers.clear();
                    const std::string label = "unário em memória " +
                                              std::to_string(base) + " tamanho " + std::to_string(size) +
                                              " modo " + std::to_string(mode.mode) + "/" +
                                              std::to_string(mode.reg);
                    check(execute(cpu) == (size == 4 ? 20u : 12u) + mode.extra,
                          label + ": ciclos");
                    check(bus.value(0x1100, size) == expected &&
                              (cpu.state().sr & 0x1f) == flags,
                          label + ": resultado e flags");
                    const std::uint32_t nextAddress = mode.mode == 3 ? addressBase + size : mode.mode == 4 ? 0x1100
                                                                                                           : addressBase;
                    check(cpu.state().address[0] == nextAddress,
                          label + ": pós-incremento ou pré-decremento");
                    std::vector<std::pair<char, std::uint32_t>> expectedTransfers;
                    for (unsigned i = 0; i < extensions; ++i)
                        expectedTransfers.emplace_back('R', 0x10e + i * 2);
                    for (unsigned i = 0; i < (size == 4 ? 2u : 1u); ++i)
                        expectedTransfers.emplace_back(size == 1 ? 'r' : 'R',
                                                       0x1100 + i * 2);
                    expectedTransfers.emplace_back('R', 0x10e + extensions * 2);
                    for (unsigned i = 0; i < (size == 4 ? 2u : 1u); ++i)
                        expectedTransfers.emplace_back(size == 1 ? 'w' : 'W',
                                                       0x1100 + (size == 4 ? 1 - i : i) * 2);
                    check(bus.transfers == expectedTransfers,
                          label + ": ordem do barramento");
                }
    }

    void testSetCases()
    {
        for (const auto [initial, expectedFlags] :
             {std::pair<std::uint16_t, unsigned>{0x7000, 0x04},
              std::pair<std::uint16_t, unsigned>{0x7080, 0x08}})
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, initial);
            bus.putWord(0x102, 0x4ac0); // TAS D0.
            Moto68000 cpu(bus);
            boot(cpu);
            execute(cpu);
            check(execute(cpu) == 4 && (cpu.state().data[0] & 0xff) == 0x80 &&
                      (cpu.state().sr & 0x0f) == expectedFlags,
                  "TAS Dn testa o valor original e ajusta o bit 7");
        }

        Bus memory;
        memory.setup();
        memory.putWord(0x100, 0x41f8); // LEA $1100,A0.
        memory.putWord(0x102, 0x1100);
        memory.putWord(0x104, 0x4ad0); // TAS (A0).
        memory.bytes[0x1100] = 0;
        Moto68000 memoryCpu(memory);
        boot(memoryCpu);
        execute(memoryCpu);
        memory.transfers.clear();
        check(execute(memoryCpu) == 14 && memory.byte(0x1100) == 0x80 &&
                  (memoryCpu.state().sr & 0x0f) == 0x04,
              "TAS em memória lê e modifica um byte");
        check(memory.transfers ==
                  std::vector<std::pair<char, std::uint32_t>>({{'r', 0x1100}, {'w', 0x1100}, {'R', 0x108}}),
              "TAS mantém leitura e escrita juntas antes da pré-busca");

        Bus stack;
        stack.setup();
        stack.putWord(0x100, 0x4adf); // TAS (A7)+.
        stack.bytes[0x1800] = 1;
        Moto68000 stackCpu(stack);
        boot(stackCpu);
        check(execute(stackCpu) == 14 && stack.byte(0x1800) == 0x81 &&
                  stackCpu.state().address[7] == 0x1802 && (stackCpu.state().sr & 0x0f) == 0,
              "TAS byte em A7 avança dois bytes");

        Bus invalid;
        invalid.setup();
        invalid.putWord(0x100, 0x4ac8); // TAS A0 não possui esse modo de endereço.
        Moto68000 invalidCpu(invalid);
        boot(invalidCpu);
        for (unsigned i = 0; i < 80 && invalidCpu.state().pc != 0x200; ++i)
            invalidCpu.clock();
        check(invalidCpu.state().pc == 0x200 && invalidCpu.state().instructions == 0,
              "TAS com An usa vetor de instrução ilegal");
    }

    void boundariesAndFaults()
    {
        for (bool predecrement : {false, true})
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, 0x4ff8); // LEA endereço,A7.
            bus.putWord(0x102, predecrement ? 0x1102 : 0x1100);
            bus.putWord(0x104, 0x74ff);
            bus.putWord(0x106, 0x5202); // X=1 e Z=1.
            bus.putWord(0x108, predecrement ? 0x4027 : 0x461f);
            bus.bytes[0x1100] = predecrement ? 0 : 0xff;
            Moto68000 cpu(bus);
            boot(cpu);
            for (unsigned i = 0; i < 3; ++i)
                execute(cpu);
            bus.transfers.clear();
            check(execute(cpu) == (predecrement ? 14u : 12u) &&
                      cpu.state().address[7] == (predecrement ? 0x1100u : 0x1102u) &&
                      bus.byte(0x1100) == (predecrement ? 0xff : 0) &&
                      (cpu.state().sr & 0x1f) == (predecrement ? 0x19 : 0x14),
                  "NEGX/NOT em A7 avançam dois bytes");
            check(bus.transfers ==
                      std::vector<std::pair<char, std::uint32_t>>({{'r', 0x1100}, {'R', 0x10c}, {'w', 0x1100}}),
                  "NEGX/NOT em A7: leitura, pré-busca e escrita");
        }

        Bus wait;
        wait.setup();
        wait.putWord(0x100, 0x41f8);
        wait.putWord(0x102, 0x1100);
        wait.putWord(0x104, 0x4450); // NEG.W (A0).
        wait.putWord(0x1100, 1);
        Moto68000 waitCpu(wait);
        boot(waitCpu);
        execute(waitCpu);
        wait.transfers.clear();
        wait.waitAddress = 0x1100;
        wait.writeWaits = 2;
        check(execute(waitCpu) == 14 && wait.word(0x1100) == 0xffff &&
                  (waitCpu.state().sr & 0x1f) == 0x19,
              "espera de escrita prolonga NEG.W em dois ciclos");
        check(wait.transfers ==
                  std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x108}, {'W', 0x1100}}),
              "espera de escrita não duplica a transferência");

        Bus overflow;
        overflow.setup();
        overflow.putWord(0x100, 0x41f8);
        overflow.putWord(0x102, 0x1100);
        overflow.putWord(0x104, 0x4490); // NEG.L (A0).
        overflow.putWord(0x1100, 0x8000);
        overflow.putWord(0x1102, 0);
        Moto68000 overflowCpu(overflow);
        boot(overflowCpu);
        execute(overflowCpu);
        check(execute(overflowCpu) == 20 &&
                  overflow.value(0x1100, 4) == 0x80000000u &&
                  (overflowCpu.state().sr & 0x1f) == 0x1b,
              "NEG.L do menor inteiro gera overflow");

        for (std::uint16_t instruction : {std::uint16_t(0x4408),
                                          std::uint16_t(0x467a),
                                          std::uint16_t(0x403c)})
        {
            Bus invalid;
            invalid.setup();
            invalid.putWord(0x100, instruction);
            Moto68000 cpu(invalid);
            boot(cpu);
            unsigned cycles = 0;
            while (cpu.state().pc != 0x200 && cycles++ < 100)
                cpu.clock();
            check(cpu.state().pc == 0x200 && cpu.state().instructions == 0,
                  "unário com destino inválido deve usar vetor 4");
        }
    }
}

int main()
{
    try
    {
        registerCases();
        memoryModes();
        testSetCases();
        boundariesAndFaults();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
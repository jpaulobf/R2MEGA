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
        std::uint32_t value(std::uint32_t address, unsigned size) const
        {
            return size == 1 ? byte(address) : size == 2 ? word(address)
                                                         : (std::uint32_t(word(address)) << 16) | word(address + 2);
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
    void prepareRegisters(Bus &bus, std::uint16_t instruction)
    {
        bus.setup();
        bus.putWord(0x100, 0x70ff); // MOVEQ #-1,D0.
        bus.putWord(0x102, 0x5200); // ADDQ.B #1,D0: X=1, D0=0xffffff00.
        bus.putWord(0x104, 0x72ff); // MOVEQ #-1,D1: X permanece ligado.
        bus.putWord(0x106, instruction);
    }
    void readyRegisters(Moto68000 &cpu)
    {
        boot(cpu);
        execute(cpu);
        execute(cpu);
        execute(cpu);
        check(cpu.state().data[0] == 0xffffff00u &&
                  cpu.state().data[1] == 0xffffffffu &&
                  (cpu.state().sr & 0x10) != 0,
              "preparo de registradores para operação lógica");
    }
    std::uint16_t standardOpcode(unsigned family, unsigned size,
                                 bool memoryTarget, unsigned mode, unsigned reg)
    {
        const unsigned sizeCode = size == 1 ? 0 : size == 2 ? 1
                                                            : 2;
        const unsigned opmode = sizeCode + (memoryTarget ? 4 : 0);
        const unsigned dataReg = memoryTarget ? (family == 0xb000 ? 1 : 0) : 0;
        return static_cast<std::uint16_t>(family | (dataReg << 9) |
                                          (opmode << 6) | (mode << 3) | reg);
    }

    void registers()
    {
        struct Case
        {
            unsigned family, size;
            std::uint32_t result;
            unsigned flags;
        };
        constexpr std::array<Case, 9> cases{{{0xc000, 1, 0xffffff00u, 0x14},
                                             {0xc000, 2, 0xffffff00u, 0x18},
                                             {0xc000, 4, 0xffffff00u, 0x18},
                                             {0x8000, 1, 0xffffffffu, 0x18},
                                             {0x8000, 2, 0xffffffffu, 0x18},
                                             {0x8000, 4, 0xffffffffu, 0x18},
                                             {0xb000, 1, 0xffffffffu, 0x18},
                                             {0xb000, 2, 0xffff00ffu, 0x10},
                                             {0xb000, 4, 0x000000ffu, 0x10}}};
        for (const auto &test : cases)
        {
            Bus bus;
            prepareRegisters(bus, standardOpcode(test.family, test.size,
                                                 test.family == 0xb000, 0, test.family == 0xb000 ? 0 : 1));
            Moto68000 cpu(bus);
            readyRegisters(cpu);
            bus.transfers.clear();
            const std::string label = "lógica registrador família " +
                                      std::to_string(test.family) + " tamanho " + std::to_string(test.size);
            check(execute(cpu) == (test.size == 4 ? 8u : 4u), label + ": ciclos");
            check(cpu.state().data[0] == test.result &&
                      cpu.state().data[1] == 0xffffffffu &&
                      (cpu.state().sr & 0x1f) == test.flags,
                  label + ": resultado e flags");
            check(bus.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10a}}), label + ": pré-busca");
        }
    }

    void memory()
    {
        struct Case
        {
            unsigned family, size;
            bool target;
            std::uint32_t result;
            unsigned flags;
        };
        constexpr std::array<Case, 6> cases{{{0x8000, 1, false, 0xffffffffu, 0x18},
                                             {0xc000, 2, false, 0xffffff00u, 0x18},
                                             {0x8000, 4, false, 0xffffffffu, 0x18},
                                             {0x8000, 1, true, 0xff, 0x18},
                                             {0xc000, 2, true, 0xff00, 0x18},
                                             {0xb000, 4, true, 0, 0x14}}};
        for (const auto &test : cases)
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, 0x41f8);
            bus.putWord(0x102, 0x1100); // LEA memória,A0.
            bus.putWord(0x104, 0x70ff);
            bus.putWord(0x106, 0x5200);
            bus.putWord(0x108, 0x72ff);
            bus.putWord(0x10a, standardOpcode(test.family, test.size,
                                              test.target, 2, 0));
            bus.putValue(0x1100, test.size, test.target ? test.family == 0xb000 ? 0xffffffffu : 0xffffu : 0xffffffffu);
            Moto68000 cpu(bus);
            boot(cpu);
            for (unsigned i = 0; i < 4; ++i)
                execute(cpu);
            bus.transfers.clear();
            const std::string label = "lógica memória família " +
                                      std::to_string(test.family) + " tamanho " + std::to_string(test.size);
            check(execute(cpu) == (test.target ? test.size == 4 ? 20u : 12u : test.size == 4 ? 14u
                                                                                             : 8u),
                  label + ": ciclos");
            check((test.target ? bus.value(0x1100, test.size) : cpu.state().data[0]) ==
                          test.result &&
                      (cpu.state().sr & 0x1f) == test.flags,
                  label + ": resultado e flags");
            check(cpu.state().address[0] == 0x1100, label + ": A0 preservado");
            std::vector<std::pair<char, std::uint32_t>> expected;
            for (unsigned i = 0; i < (test.size == 4 ? 2u : 1u); ++i)
                expected.emplace_back(test.size == 1 ? 'r' : 'R', 0x1100 + i * 2);
            expected.emplace_back('R', 0x10e);
            if (test.target)
                for (unsigned i = 0; i < (test.size == 4 ? 2u : 1u); ++i)
                    expected.emplace_back(test.size == 1 ? 'w' : 'W', 0x1100 + i * 2);
            check(bus.transfers == expected, label + ": ordem do barramento");
        }
    }

    void immediate()
    {
        struct Case
        {
            std::uint16_t opcode, high, low;
            unsigned size, cycles, flags;
            std::uint32_t result;
        };
        constexpr std::array<Case, 3> cases{{{0x0000, 0x0080, 0, 1, 8, 0x18, 0xffffff80u},
                                             {0x0240, 0x00ff, 0, 2, 8, 0x14, 0xffff0000u},
                                             {0x0a80, 0xffff, 0xffff, 4, 16, 0x10, 0x000000ffu}}};
        for (const auto &test : cases)
        {
            Bus bus;
            prepareRegisters(bus, test.opcode);
            bus.putWord(0x108, test.high);
            if (test.size == 4)
                bus.putWord(0x10a, test.low);
            Moto68000 cpu(bus);
            readyRegisters(cpu);
            bus.transfers.clear();
            check(execute(cpu) == test.cycles &&
                      cpu.state().data[0] == test.result &&
                      (cpu.state().sr & 0x1f) == test.flags,
                  "ORI/ANDI/EORI em D0: ciclos, resultado e flags");
            std::vector<std::pair<char, std::uint32_t>> expected{{'R', 0x10a}};
            if (test.size == 4)
                expected.emplace_back('R', 0x10c);
            expected.emplace_back('R', test.size == 4 ? 0x10e : 0x10c);
            check(bus.transfers == expected,
                  "ORI/ANDI/EORI em D0: extensões antes da pré-busca");
        }

        Bus memoryBus;
        memoryBus.setup();
        memoryBus.putWord(0x100, 0x41f8);
        memoryBus.putWord(0x102, 0x1100);
        memoryBus.putWord(0x104, 0x70ff);
        memoryBus.putWord(0x106, 0x5200);
        memoryBus.putWord(0x108, 0x0a50); // EORI.W #$ffff,(A0).
        memoryBus.putWord(0x10a, 0xffff);
        memoryBus.putWord(0x1100, 0xffff);
        Moto68000 cpu(memoryBus);
        boot(cpu);
        execute(cpu);
        execute(cpu);
        execute(cpu);
        memoryBus.transfers.clear();
        check(execute(cpu) == 16 && memoryBus.word(0x1100) == 0 &&
                  (cpu.state().sr & 0x1f) == 0x14,
              "EORI.W em memória preserva X e atualiza Z");
        check(memoryBus.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10c}, {'R', 0x1100}, {'R', 0x10e}, {'W', 0x1100}}),
              "EORI.W em memória: extensão, leitura, pré-busca e escrita");
    }

    void addressingAndInvalidModes()
    {
        Bus relative;
        relative.setup();
        relative.putWord(0x100, 0x7000); // MOVEQ #0,D0.
        relative.putWord(0x102, 0x807a); // OR.W (d16,PC),D0.
        relative.putWord(0x104, 0x0ffc); // PC da extensão em $104; destino em $1100.
        relative.putWord(0x1100, 0x8000);
        Moto68000 relativeCpu(relative);
        boot(relativeCpu);
        execute(relativeCpu);
        relative.transfers.clear();
        check(execute(relativeCpu) == 12 && relativeCpu.state().data[0] == 0x8000 &&
                  (relativeCpu.state().sr & 0x1f) == 8 && relative.word(0x1100) == 0x8000,
              "OR.W relativo ao PC lê sem modificar a origem");
        check(relative.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x106}, {'R', 0x1100}, {'R', 0x108}}),
              "OR.W relativo ao PC: extensão antes do operando");

        Bus indexed;
        indexed.setup();
        indexed.putWord(0x100, 0x41f8);
        indexed.putWord(0x102, 0x1100);
        indexed.putWord(0x104, 0x7202); // D1.W = 2.
        indexed.putWord(0x106, 0x70ff); // D0.B = $ff.
        indexed.putWord(0x108, 0xb130); // EOR.B D0,(d8,A0,D1.W).
        indexed.putWord(0x10a, 0x1002); // Deslocamento 2 + índice 2.
        indexed.bytes[0x1104] = 0xff;
        Moto68000 indexedCpu(indexed);
        boot(indexedCpu);
        execute(indexedCpu);
        execute(indexedCpu);
        execute(indexedCpu);
        indexed.transfers.clear();
        check(execute(indexedCpu) == 18 && indexed.byte(0x1104) == 0 &&
                  indexedCpu.state().address[0] == 0x1100 &&
                  (indexedCpu.state().sr & 0x1f) == 4,
              "EOR.B indexado calcula o destino e atualiza Z");
        check(indexed.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10c}, {'r', 0x1104}, {'R', 0x10e}, {'w', 0x1104}}),
              "EOR.B indexado: extensão, leitura, pré-busca e escrita");

        for (std::uint16_t instruction : {std::uint16_t(0x8008),
                                          std::uint16_t(0xb13a),
                                          std::uint16_t(0x0248)})
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
                  "modo inválido de operação lógica deve usar vetor 4");
        }
    }
}

int main()
{
    try
    {
        registers();
        memory();
        immediate();
        addressingAndInvalidModes();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
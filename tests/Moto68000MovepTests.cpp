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
        unsigned readWaits = 0, writeWaits = 0;

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

        std::uint16_t readWord(std::uint32_t address) override
        {
            transfers.emplace_back('R', address);
            return static_cast<std::uint16_t>((std::uint16_t(byte(address)) << 8) |
                                              byte(address + 1));
        }

        std::uint8_t readByte(std::uint32_t address) override
        {
            transfers.emplace_back('r', address);
            return byte(address);
        }

        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            transfers.emplace_back('W', address);
            putWord(address, value);
        }

        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            transfers.emplace_back('w', address);
            bytes[address] = value;
        }

        bool tryReadByte(std::uint32_t address, std::uint8_t &value) override
        {
            if (address == waitAddress && readWaits)
            {
                --readWaits;
                return false;
            }
            value = readByte(address);
            return true;
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

        void setup(std::uint16_t opcode, std::uint16_t base,
                   std::uint16_t displacement,
                   unsigned dataReg = 0, unsigned addressReg = 0)
        {
            putWord(0, 0);
            putWord(2, 0x1800);
            putWord(4, 0);
            putWord(6, 0x100);
            // MOVE.L #$12345678,Dn.
            putWord(0x100, static_cast<std::uint16_t>(0x203c | (dataReg << 9)));
            putWord(0x102, 0x1234);
            putWord(0x104, 0x5678);
            // LEA base.W,An.
            putWord(0x106, static_cast<std::uint16_t>(0x41f8 | (addressReg << 9)));
            putWord(0x108, base);
            putWord(0x10a, 0x7480);
            putWord(0x10c, 0x4402); // X,N,V,C = 1.
            putWord(0x10e, opcode);
            putWord(0x110, displacement);
            putWord(0x112, 0x4e71);
            putWord(0x114, 0x4e71);
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
        while (cpu.state().instructions == previous && cycles < 100)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == previous + 1,
              "instrução não terminou");
        return cycles;
    }

    void movepCases()
    {
        struct Case
        {
            bool write, longSize, odd;
            std::uint32_t result;
        };
        constexpr std::array<Case, 8> cases{{{false, false, false, 0x1234aabb},
                                             {false, true, false, 0xaabbccdd},
                                             {true, false, false, 0x12345678},
                                             {true, true, false, 0x12345678},
                                             {false, false, true, 0x1234aabb},
                                             {false, true, true, 0xaabbccdd},
                                             {true, false, true, 0x12345678},
                                             {true, true, true, 0x12345678}}};
        for (const auto &test : cases)
        {
            Bus bus;
            const std::uint16_t base = test.odd ? 0x1100 : 0x10fe;
            const std::uint16_t displacement = test.odd ? 1 : 2;
            const std::uint32_t target = base + displacement;
            const std::uint16_t opcode = static_cast<std::uint16_t>(
                0x0108 | (test.write ? 0x80 : 0) |
                (test.longSize ? 0x40 : 0));
            bus.setup(opcode, base, displacement);
            for (unsigned i = 0; i < 4; ++i)
            {
                bus.bytes[target + i * 2] = static_cast<std::uint8_t>(
                    0xaa + i * 0x11);
                bus.bytes[target + i * 2 + 1] = 0xee;
            }
            Moto68000 cpu(bus);
            cpu.reset();
            for (unsigned i = 0; i < 40; ++i)
                cpu.clock();
            check(cpu.resetComplete(), "reset incompleto");
            for (unsigned i = 0; i < 4; ++i)
                execute(cpu);
            bus.transfers.clear();
            const std::string label = std::string("MOVEP ") +
                                      (test.write ? "registrador para memória" : "memória para registrador") +
                                      (test.longSize ? " longo" : " palavra") +
                                      (test.odd ? " ímpar" : " par");
            check(execute(cpu) == (test.longSize ? 24u : 16u) &&
                      cpu.state().data[0] == test.result &&
                      cpu.state().address[0] == base &&
                      (cpu.state().sr & 0x1f) == 0x1b &&
                      cpu.state().pc == 0x112,
                  label + ": estado e ciclos");
            std::vector<std::pair<char, std::uint32_t>> expected{{'R', 0x112}};
            const unsigned count = test.longSize ? 4 : 2;
            for (unsigned i = 0; i < count; ++i)
            {
                expected.emplace_back(test.write ? 'w' : 'r',
                                      target + i * 2);
                const std::uint8_t byte = test.write ? static_cast<std::uint8_t>((0x12345678u >>
                                                                                  (test.longSize ? 24 - i * 8 : 8 - i * 8)) &
                                                                                 0xff)
                                                     : static_cast<std::uint8_t>(0xaa + i * 0x11);
                check(bus.byte(target + i * 2) == byte,
                      label + ": ordem dos bytes");
                check(bus.byte(target + i * 2 + 1) == 0xee,
                      label + ": byte vizinho preservado");
            }
            expected.emplace_back('R', 0x114);
            check(bus.transfers == expected,
                  label + ": ordem do barramento");
            check(execute(cpu) == 4 && cpu.state().pc == 0x114,
                  label + ": próxima instrução");
        }
    }

    void waitCycles()
    {
        for (bool write : {false, true})
        {
            Bus bus;
            bus.setup(static_cast<std::uint16_t>(0x0108 | (write ? 0x80 : 0)),
                      0x1100, 0);
            bus.bytes[0x1100] = 0xaa;
            bus.bytes[0x1102] = 0xbb;
            Moto68000 cpu(bus);
            cpu.reset();
            for (unsigned i = 0; i < 40; ++i)
                cpu.clock();
            check(cpu.resetComplete(), "reset incompleto");
            for (unsigned i = 0; i < 4; ++i)
                execute(cpu);
            bus.transfers.clear();
            bus.waitAddress = 0x1102;
            if (write)
                bus.writeWaits = 2;
            else
                bus.readWaits = 2;
            check(execute(cpu) == 18 &&
                      cpu.state().data[0] == (write ? 0x12345678u : 0x1234aabbu),
                  "MOVEP respeita duas esperas no segundo byte");
            check(bus.transfers ==
                      std::vector<std::pair<char, std::uint32_t>>({{'R', 0x112}, {write ? 'w' : 'r', 0x1100}, {write ? 'w' : 'r', 0x1102}, {'R', 0x114}}),
                  "espera de MOVEP não duplica a transferência");
        }
    }

    void signedDisplacementAndRegisters()
    {
        Bus bus;
        bus.setup(static_cast<std::uint16_t>(0x0108 | (3 << 9) | 5),
                  0x1102, 0xfffe, 3, 5); // MOVEP.W (-2,A5),D3.
        bus.bytes[0x1100] = 0xab;
        bus.bytes[0x1102] = 0xcd;
        Moto68000 cpu(bus);
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        check(cpu.resetComplete(), "reset incompleto");
        for (unsigned i = 0; i < 4; ++i)
            execute(cpu);
        bus.transfers.clear();
        check(execute(cpu) == 16 &&
                  cpu.state().data[3] == 0x1234abcd &&
                  cpu.state().address[5] == 0x1102 &&
                  (cpu.state().sr & 0x1f) == 0x1b,
              "MOVEP.W usa D3, A5 e deslocamento negativo");
        check(bus.transfers ==
                  std::vector<std::pair<char, std::uint32_t>>({{'R', 0x112}, {'r', 0x1100}, {'r', 0x1102}, {'R', 0x114}}),
              "MOVEP.W acessa a faixa indicada pelo deslocamento negativo");
    }
}

int main()
{
    try
    {
        movepCases();
        waitCycles();
        signedDisplacementAndRegisters();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#include "Core/CPU/Moto68000/Moto68000.h"
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

        void putLong(std::uint32_t address, std::uint32_t value)
        {
            words[address] = static_cast<std::uint16_t>(value >> 16);
            words[address + 2] = static_cast<std::uint16_t>(value);
        }

        std::uint16_t readWord(std::uint32_t address) override
        {
            reads.push_back(address);
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
            words[address] = value;
        }

        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            const std::uint32_t aligned = address & ~1u;
            const std::uint16_t previous = words[aligned];
            words[aligned] = address & 1 ? static_cast<std::uint16_t>((previous & 0xff00) | value) : static_cast<std::uint16_t>((previous & 0x00ff) | (value << 8));
        }

        void setup(std::uint16_t opcode, std::uint32_t first,
                   std::uint32_t second)
        {
            putLong(0, 0x1800);
            putLong(4, 0x100);
            words[0x100] = 0x203c; // MOVE.L #valor,D0.
            putLong(0x102, first);
            words[0x106] = 0x223c; // MOVE.L #valor,D1.
            putLong(0x108, second);
            words[0x10c] = 0x41f8; // LEA $1700.W,A0.
            words[0x10e] = 0x1700;
            words[0x110] = 0x43f8; // LEA $1600.W,A1.
            words[0x112] = 0x1600;
            words[0x114] = 0x7480; // MOVEQ #-128,D2.
            words[0x116] = 0x4402; // NEG.B D2 prepara X,N,V,C.
            words[0x118] = opcode;
            words[0x11a] = 0x4e71;
            words[0x11c] = 0x4e71;
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

    void verify(std::uint16_t opcode, std::uint32_t first,
                std::uint32_t second, std::uint32_t expectedD0,
                std::uint32_t expectedD1, std::uint32_t expectedA0,
                std::uint32_t expectedA1, std::uint32_t expectedA7,
                unsigned expectedFlags, unsigned expectedCycles,
                const std::string &label)
    {
        Bus bus;
        bus.setup(opcode, first, second);
        Moto68000 cpu(bus);
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        check(cpu.resetComplete(), "reset incompleto");
        for (unsigned i = 0; i < 6; ++i)
            execute(cpu);
        check((cpu.state().sr & 0x1f) == 0x1b,
              "preparo das flags não terminou");
        bus.reads.clear();
        check(execute(cpu) == expectedCycles &&
                  cpu.state().data[0] == expectedD0 &&
                  cpu.state().data[1] == expectedD1 &&
                  cpu.state().address[0] == expectedA0 &&
                  cpu.state().address[1] == expectedA1 &&
                  cpu.state().address[7] == expectedA7 &&
                  (cpu.state().sr & 0x1f) == expectedFlags,
              label + ": registradores, flags e ciclos");
        check(bus.reads == std::vector<std::uint32_t>{0x11c} &&
                  cpu.state().pc == 0x11a,
              label + ": pré-busca");
        check(execute(cpu) == 4 && cpu.state().pc == 0x11c,
              label + ": próxima instrução");
    }

    void registerOperations()
    {
        verify(0x4840, 0x80000001, 0x12345678,
               0x00018000, 0x12345678, 0x1700, 0x1600, 0x1800,
               0x10, 4, "SWAP D0");
        verify(0x4840, 0, 0x12345678,
               0, 0x12345678, 0x1700, 0x1600, 0x1800,
               0x14, 4, "SWAP com zero");
        verify(0x4880, 0x12340080, 0x12345678,
               0x1234ff80, 0x12345678, 0x1700, 0x1600, 0x1800,
               0x18, 4, "EXT.W preserva a palavra alta");
        verify(0x4880, 0x12340000, 0x12345678,
               0x12340000, 0x12345678, 0x1700, 0x1600, 0x1800,
               0x14, 4, "EXT.W calcula Z sobre a palavra baixa");
        verify(0x48c0, 0x12348000, 0x12345678,
               0xffff8000, 0x12345678, 0x1700, 0x1600, 0x1800,
               0x18, 4, "EXT.L estende o sinal");
        verify(0x48c0, 0x12340000, 0x12345678,
               0, 0x12345678, 0x1700, 0x1600, 0x1800,
               0x14, 4, "EXT.L calcula Z sobre o long completo");
        verify(0xc141, 0x12345678, 0xabcdef12,
               0xabcdef12, 0x12345678, 0x1700, 0x1600, 0x1800,
               0x1b, 6, "EXG D0,D1");
        verify(0xc149, 0x12345678, 0xabcdef12,
               0x12345678, 0xabcdef12, 0x1600, 0x1700, 0x1800,
               0x1b, 6, "EXG A0,A1");
        verify(0xc189, 0x12345678, 0xabcdef12,
               0x1600, 0xabcdef12, 0x1700, 0x12345678, 0x1800,
               0x1b, 6, "EXG D0,A1");
        verify(0xc18f, 0x12345678, 0xabcdef12,
               0x1800, 0xabcdef12, 0x1700, 0x1600, 0x12345678,
               0x1b, 6, "EXG D0,A7");
    }
}

int main()
{
    try
    {
        registerOperations();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
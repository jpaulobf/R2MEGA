#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    struct Bus : M68KBus
    {
        std::unordered_map<std::uint32_t, std::uint8_t> bytes;
        std::vector<std::uint32_t> reads;
        std::vector<unsigned> readTicks;
        unsigned tick = 0;

        void observe(const M68KPins &) override { ++tick; }

        void word(std::uint32_t address, std::uint16_t value)
        {
            bytes[address] = static_cast<std::uint8_t>(value >> 8);
            bytes[address + 1] = static_cast<std::uint8_t>(value);
        }
        std::uint8_t readByte(std::uint32_t address) override
        {
            return bytes[address];
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            reads.push_back(address);
            readTicks.push_back(tick);
            return (std::uint16_t(bytes[address]) << 8) | bytes[address + 1];
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            bytes[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            word(address, value);
        }
        void setup(std::uint32_t destination)
        {
            word(0, 0);
            word(2, 0x1800);
            word(4, 0);
            word(6, 0x100);
            word(20, 0);
            word(22, 0x300);
            word(12, 0);
            word(14, 0x300);
            word(0x100, 0x203c); // MOVE.L #valor,D0
            word(0x102, static_cast<std::uint16_t>(destination >> 16));
            word(0x104, static_cast<std::uint16_t>(destination));
            word(0x300, 0x4e71);
            word(0x302, 0x4e71);
        }
    };

    void check(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    unsigned execute(Moto68000 &cpu)
    {
        const auto before = cpu.state().instructions;
        unsigned cycles = 0;
        while (cpu.state().instructions == before && cycles < 300)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == before + 1, "instrução não terminou");
        return cycles;
    }

    struct Result
    {
        std::uint32_t data;
        std::uint16_t sr;
        unsigned cycles;
        std::uint32_t pc;
        std::vector<std::uint32_t> reads;
        unsigned finalReadGap;
    };

    Result run(std::uint16_t opcode, std::uint32_t destination,
               std::uint16_t source, unsigned mode = 7, unsigned reg = 4)
    {
        Bus bus;
        bus.setup(destination);
        const std::uint16_t encoded = opcode | static_cast<std::uint16_t>((mode << 3) | reg);
        bus.word(0x106, encoded);
        bus.word(0x108, mode == 7 && reg == 4 ? source : 0x1200);
        bus.word(0x10a, 0x4e71);
        bus.word(0x10c, 0x4e71);
        bus.word(0x1200, source);
        Moto68000 cpu(bus);
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        check(cpu.resetComplete(), "reset incompleto");
        execute(cpu);
        bus.reads.clear();
        bus.readTicks.clear();
        const unsigned cycles = execute(cpu);
        return {cpu.state().data[0], cpu.state().sr, cycles,
                cpu.state().pc, bus.reads, bus.tick - bus.readTicks.back()};
    }

    void multiplication()
    {
        const auto zero = run(0xc0c0, 0x12340007, 0);
        check(zero.data == 0 && (zero.sr & 0x0f) == 0x04, "MULU zero/flags");
        check(zero.cycles == 42, "MULU imediato zero: ciclos");
        const auto unsignedResult = run(0xc0c0, 0xffff, 0xffff);
        check(unsignedResult.data == 0xfffe0001 && (unsignedResult.sr & 0x0f) == 0x08,
              "MULU produto de 32 bits");
        check(unsignedResult.cycles == 74, "MULU imediato com 16 bits: ciclos");
        check(unsignedResult.finalReadGap > 30, "MULU: pré-busca antes do cálculo interno");
        const auto signedResult = run(0xc1c0, 0xfffffffd, 0xfffe);
        check(signedResult.data == 6 && (signedResult.sr & 0x0f) == 0,
              "MULS negativo por negativo");
        check(signedResult.cycles == 44, "MULS: transição do bit alto da origem");
        const auto alternating = run(0xc1c0, 0x00000002, 0x5555);
        check(alternating.data == 0xaaaa && alternating.cycles == 74,
              "MULS transições da origem");
        const auto memory = run(0xc0c0, 3, 7, 7, 0);
        check(memory.data == 21 && memory.cycles == 52,
              "MULU absoluto: resultado/ciclos (obtido " + std::to_string(memory.cycles) + ")");
        check(memory.reads == std::vector<std::uint32_t>({0x10a, 0x1200, 0x10c}),
              "MULU absoluto: acessos ao barramento");
    }

    void division()
    {
        const auto unsignedResult = run(0x80c0, 0x00010003, 3);
        check(unsignedResult.data == 0x00015556 && (unsignedResult.sr & 0x0f) == 0,
              "DIVU quociente e resto");
        check(unsignedResult.cycles == 124, "DIVU: ciclos variáveis");
        check(unsignedResult.finalReadGap <= 2, "DIVU: pré-busca ao final");
        const auto signedResult = run(0x81c0, 0xfffffff9, 3);
        check(signedResult.data == 0xfffffffe && (signedResult.sr & 0x0f) == 0x08,
              "DIVS quociente e resto negativos");
        check(signedResult.cycles == 158, "DIVS: ciclos variáveis");
        check(signedResult.finalReadGap <= 2, "DIVS: pré-busca ao final");
        const auto overflow = run(0x80c0, 0x10000, 1);
        check(overflow.data == 0x10000 && (overflow.sr & 0x0f) == 0x0a,
              "DIVU estouro preserva destino e ativa N/V");
        check(overflow.cycles == 14, "DIVU estouro: ciclos");
        const auto signedOverflow = run(0x81c0, 0x80000000, 0xffff);
        check(signedOverflow.data == 0x80000000 && (signedOverflow.sr & 0x0f) == 0x0a,
              "DIVS estouro com INT32_MIN");
        const auto minimum = run(0x81c0, 0x40000000, 0x8000);
        check(minimum.data == 0x00008000 && (minimum.sr & 0x0f) == 0x08,
              "DIVS quociente -32768 permitido");
        const auto signedZero = run(0x81c0, 0, 0xffff);
        check(signedZero.data == 0 && (signedZero.sr & 0x0f) == 0x04,
              "DIVS resultado zero");
    }

    void addressing()
    {
        struct Mode
        {
            const char *name;
            unsigned mode, reg, extensions, extraCycles;
            std::uint16_t extension0, extension1;
            std::uint32_t sourceAddress;
        };
        const std::array<Mode, 11> modes = {{{"D1", 0, 1, 0, 0, 0, 0, 0},
                                             {"(A0)", 2, 0, 0, 4, 0, 0, 0x1202},
                                             {"(A0)+", 3, 0, 0, 4, 0, 0, 0x1202},
                                             {"-(A0)", 4, 0, 0, 6, 0, 0, 0x1200},
                                             {"(-2,A0)", 5, 0, 1, 8, 0xfffe, 0, 0x1200},
                                             {"(-7,A0,D1.W)", 6, 0, 1, 10, 0x10f9, 0, 0x1202},
                                             {"$1200.W", 7, 0, 1, 8, 0x1200, 0, 0x1200},
                                             {"$00001200.L", 7, 1, 2, 12, 0, 0x1200, 0x1200},
                                             {"($0ff2,PC)", 7, 2, 1, 8, 0x0ff2, 0, 0x1100},
                                             {"($2b,PC,D1.W)", 7, 3, 1, 10, 0x102b, 0, 0x140},
                                             {"#7", 7, 4, 1, 4, 7, 0, 0}}};
        for (unsigned divide = 0; divide < 2; ++divide)
        {
            unsigned registerCycles = 0;
            for (const auto &mode : modes)
            {
                Bus bus;
                bus.setup(divide ? 0x10003 : 3);
                bus.word(0x106, 0x41f8); // LEA $1202.W,A0
                bus.word(0x108, 0x1202);
                bus.word(0x10a, 0x7207); // MOVEQ #7,D1
                bus.word(0x10c, static_cast<std::uint16_t>(
                                    (divide ? 0x80c0 : 0xc0c0) |
                                    (mode.mode << 3) | mode.reg));
                bus.word(0x10e, mode.extension0);
                bus.word(0x110, mode.extension1);
                bus.word(0x112, 0x4e71);
                bus.word(0x114, 0x4e71);
                if (mode.sourceAddress)
                    bus.word(mode.sourceAddress, 7);
                Moto68000 cpu(bus);
                cpu.reset();
                for (unsigned i = 0; i < 40; ++i)
                    cpu.clock();
                execute(cpu);
                execute(cpu);
                execute(cpu);
                const unsigned cycles = execute(cpu);
                check(cpu.state().data[0] == (divide ? 0x00052492u : 21u),
                      std::string(divide ? "DIVU " : "MULU ") + mode.name + ": resultado");
                if (mode.mode == 0)
                    registerCycles = cycles;
                check(cycles == registerCycles + mode.extraCycles,
                      std::string(divide ? "DIVU " : "MULU ") + mode.name +
                          ": ciclos (obtido " + std::to_string(cycles) + ")");
                if (mode.mode == 3 || mode.mode == 4)
                    check(cpu.state().address[0] == (mode.mode == 3 ? 0x1204u : 0x1200u),
                          std::string(mode.name) + ": atualização de A0");
            }
        }
    }

    void zeroDivision()
    {
        Bus bus;
        bus.setup(0x12345678);
        bus.word(0x106, 0x80fc); // DIVU.W #0,D0
        bus.word(0x108, 0);
        Moto68000 cpu(bus);
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        execute(cpu);
        for (unsigned i = 0; i < 100 && cpu.state().pc != 0x300; ++i)
            cpu.clock();
        check(cpu.state().pc == 0x300, "DIVU por zero: vetor 5");
        check(cpu.state().data[0] == 0x12345678, "DIVU por zero preserva D0");
        check(bus.readWord(0x17fc) == 0 && bus.readWord(0x17fe) == 0x10a,
              "DIVU por zero empilha PC seguinte");
    }

    void addressError()
    {
        Bus bus;
        bus.setup(3);
        bus.word(0x106, 0x41f8); // LEA $1201.W,A0
        bus.word(0x108, 0x1201);
        bus.word(0x10a, 0xc0d8); // MULU.W (A0)+,D0
        bus.word(0x10c, 0x4e71);
        Moto68000 cpu(bus);
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        execute(cpu);
        execute(cpu);
        const auto before = cpu.state().cycles;
        for (unsigned i = 0; i < 100 && cpu.state().pc != 0x300; ++i)
            cpu.clock();
        check(cpu.state().pc == 0x300 && cpu.state().cycles - before == 58,
              "MULU desalinhado: vetor e ciclos");
        check(cpu.state().address[0] == 0x1203, "pós-incremento antes da falha");
        check(bus.readWord(0x17fe) == 0x10c && bus.readWord(0x17fc) == 0 &&
                  bus.readWord(0x17fa) == 0x2700 && bus.readWord(0x17f8) == 0xc0d8 &&
                  bus.readWord(0x17f6) == 0x1201 && bus.readWord(0x17f4) == 0 &&
                  bus.readWord(0x17f2) == 0xc0d5,
              "MULU desalinhado: quadro de exceção");
    }
}

int main()
{
    try
    {
        multiplication();
        division();
        addressing();
        zeroDivision();
        addressError();
        std::cout << "Multiplicação e divisão do 68000: OK\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
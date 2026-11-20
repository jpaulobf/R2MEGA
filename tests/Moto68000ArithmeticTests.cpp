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
        std::uint32_t faultAddress = 0xffffffffu;
        bool faultWrite = false;
        unsigned readWait = 0, writeWait = 0;

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
            if (!faultWrite && address == faultAddress)
                throw M68KBusError("falha de leitura de byte");
            transfers.emplace_back('r', address);
            return byte(address);
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            if (!faultWrite && address == faultAddress)
                throw M68KBusError("falha de leitura de word");
            transfers.emplace_back('R', address);
            return word(address);
        }
        bool tryReadByte(std::uint32_t address, std::uint8_t &value) override
        {
            if (address == waitAddress && readWait)
            {
                --readWait;
                return false;
            }
            value = readByte(address);
            return true;
        }
        bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
        {
            if (address == waitAddress && readWait)
            {
                --readWait;
                return false;
            }
            value = readWord(address);
            return true;
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            if (faultWrite && address == faultAddress)
                throw M68KBusError("falha de escrita de byte");
            transfers.emplace_back('w', address);
            bytes[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            if (faultWrite && address == faultAddress)
                throw M68KBusError("falha de escrita de word");
            transfers.emplace_back('W', address);
            putWord(address, value);
        }
        bool tryWriteByte(std::uint32_t address, std::uint8_t value) override
        {
            if (address == waitAddress && writeWait)
            {
                --writeWait;
                return false;
            }
            writeByte(address, value);
            return true;
        }
        bool tryWriteWord(std::uint32_t address, std::uint16_t value) override
        {
            if (address == waitAddress && writeWait)
            {
                --writeWait;
                return false;
            }
            writeWord(address, value);
            return true;
        }
        void setup(std::uint16_t stack = 0x1800)
        {
            putWord(0, 0);
            putWord(2, stack);
            putWord(4, 0);
            putWord(6, 0x100);
            putWord(8, 0);
            putWord(10, 0x400);
            putWord(12, 0);
            putWord(14, 0x200);
            putWord(16, 0);
            putWord(18, 0x300);
            for (unsigned address = 0x100; address < 0x140; address += 2)
                putWord(address, 0x4e71);
            putWord(0x200, 0x4e71);
            putWord(0x202, 0x4e71);
            putWord(0x300, 0x4e71);
            putWord(0x302, 0x4e71);
            putWord(0x400, 0x4e71);
            putWord(0x402, 0x4e71);
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
        for (unsigned cycle = 0; cycle < 40; ++cycle)
            cpu.clock();
        check(cpu.resetComplete(), "reset incompleto");
    }

    unsigned execute(Moto68000 &cpu)
    {
        const auto before = cpu.state().instructions;
        unsigned cycles = 0;
        while (cpu.state().instructions == before && cycles < 200)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == before + 1, "instrução não terminou");
        return cycles;
    }

    void runUntilPc(Moto68000 &cpu, std::uint32_t target)
    {
        unsigned cycles = 0;
        while (cpu.state().pc != target && cycles++ < 100)
            cpu.clock();
        check(cpu.state().pc == target, "tratador de exceção não foi carregado");
    }

    void checkAddressingMatrix()
    {
        struct Addressing
        {
            const char *name;
            unsigned mode, reg, words, byteWordCycles, longCycles;
        };
        // Tempos de cálculo do endereço efetivo na tabela 8-1 do MC68000.
        const std::array<Addressing, 10> modes = {{{"(A0)", 2, 0, 0, 4, 8},
                                                   {"(A0)+", 3, 0, 0, 4, 8},
                                                   {"-(A0)", 4, 0, 0, 6, 10},
                                                   {"(4,A0)", 5, 0, 1, 8, 12},
                                                   {"(3,A0,D0.W)", 6, 0, 1, 10, 14},
                                                   {"$1100.W", 7, 0, 1, 8, 12},
                                                   {"$00001100.L", 7, 1, 2, 12, 16},
                                                   {"(24,PC)", 7, 2, 1, 8, 12},
                                                   {"(23,PC,D0.W)", 7, 3, 1, 10, 14},
                                                   {"#1", 7, 4, 1, 4, 8}}};

        for (unsigned subtract = 0; subtract < 2; ++subtract)
        {
            for (unsigned memoryDestination = 0; memoryDestination < 2; ++memoryDestination)
            {
                for (unsigned sizeCode = 0; sizeCode < 3; ++sizeCode)
                {
                    const unsigned size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                                            : 4;
                    for (const auto &ea : modes)
                    {
                        if (memoryDestination && ea.mode == 7 && ea.reg >= 2)
                            continue;
                        Bus bus;
                        bus.setup();
                        bus.putWord(0x100, 0x41f8);
                        bus.putWord(0x102, 0x1100);
                        bus.putWord(0x104, 0x7001); // MOVEQ #1,D0
                        const unsigned opmode = sizeCode + (memoryDestination ? 4 : 0);
                        const std::uint16_t opcode =
                            (subtract ? 0x9000 : 0xd000) | (opmode << 6) |
                            (ea.mode << 3) | ea.reg;
                        bus.putWord(0x106, opcode);
                        unsigned extensions = ea.words;
                        if (ea.mode == 5)
                            bus.putWord(0x108, 4);
                        else if (ea.mode == 6)
                            bus.putWord(0x108, 3);
                        else if (ea.mode == 7 && ea.reg == 0)
                            bus.putWord(0x108, 0x1100);
                        else if (ea.mode == 7 && ea.reg == 1)
                        {
                            bus.putWord(0x108, 0);
                            bus.putWord(0x10a, 0x1100);
                        }
                        else if (ea.mode == 7 && ea.reg == 2)
                            bus.putWord(0x108, 0x18);
                        else if (ea.mode == 7 && ea.reg == 3)
                            bus.putWord(0x108, 0x17);
                        else if (ea.mode == 7 && ea.reg == 4)
                        {
                            if (size == 4)
                            {
                                bus.putWord(0x108, 0);
                                bus.putWord(0x10a, 1);
                                extensions = 2;
                            }
                            else
                                bus.putWord(0x108, 1);
                        }

                        std::uint32_t target = 0x1100;
                        if (ea.mode == 4)
                            target -= size;
                        else if (ea.mode == 5 || ea.mode == 6)
                            target += 4;
                        else if (ea.mode == 7 && (ea.reg == 2 || ea.reg == 3))
                            target = 0x120;
                        if (!(ea.mode == 7 && ea.reg == 4))
                        {
                            bus.putWord(target, size == 1 ? 0x0100 : size == 2 ? 1
                                                                               : 0);
                            if (size == 4)
                                bus.putWord(target + 2, 1);
                        }

                        Moto68000 cpu(bus);
                        boot(cpu);
                        check(execute(cpu) == 8 && execute(cpu) == 4,
                              "preparação de ADD/SUB");
                        bus.transfers.clear();
                        const unsigned eaCycles = size == 4 ? ea.longCycles : ea.byteWordCycles;
                        const unsigned baseCycles = memoryDestination ? (size == 4 ? 12 : 8) : size == 4 ? (ea.mode == 7 && ea.reg == 4 ? 8 : 6)
                                                                                                         : 4;
                        const std::string label = std::string(subtract ? "SUB." : "ADD.") +
                                                  (size == 1 ? "B " : size == 2 ? "W "
                                                                                : "L ") +
                                                  ea.name +
                                                  (memoryDestination ? " destino" : " origem");
                        check(execute(cpu) == baseCycles + eaCycles,
                              label + ": ciclos");
                        const std::uint32_t result = subtract ? 0 : 2;
                        check(cpu.state().pc == 0x108 + extensions * 2 &&
                                  (cpu.state().sr & 0x1f) == (subtract ? 4 : 0),
                              label + ": PC ou flags");
                        const int step = ea.mode == 4 ? -static_cast<int>(size) : ea.mode == 3 ? static_cast<int>(size)
                                                                                               : 0;
                        check(cpu.state().address[0] == 0x1100 + step,
                              label + ": atualização de A0");
                        if (memoryDestination)
                        {
                            check(cpu.state().data[0] == 1 &&
                                      (size == 1 ? bus.byte(target) == result : size == 2 ? bus.word(target) == result
                                                                                          : bus.word(target) == 0 && bus.word(target + 2) == result),
                                  label + ": resultado na memória");
                        }
                        else
                            check(cpu.state().data[0] == result,
                                  label + ": resultado em D0");

                        std::vector<std::pair<char, std::uint32_t>> expected;
                        for (unsigned word = 0; word < extensions; ++word)
                            expected.emplace_back('R', 0x10a + word * 2);
                        if (!(ea.mode == 7 && ea.reg == 4))
                        {
                            expected.emplace_back(size == 1 ? 'r' : 'R', target);
                            if (size == 4)
                                expected.emplace_back('R', target + 2);
                        }
                        expected.emplace_back('R', 0x10a + extensions * 2);
                        if (memoryDestination)
                        {
                            if (size == 4)
                                expected.emplace_back('W', target + 2);
                            expected.emplace_back(size == 1 ? 'w' : 'W', target);
                        }
                        check(bus.transfers == expected, label + ": ordem do barramento");
                    }
                }
            }
        }
    }

    void checkRegisterFlags()
    {
        Bus registers;
        registers.setup();
        registers.putWord(0x100, 0x7001); // MOVEQ #1,D0
        registers.putWord(0x102, 0x7202); // MOVEQ #2,D1
        registers.putWord(0x104, 0xd081); // ADD.L D1,D0
        registers.putWord(0x106, 0x9081); // SUB.L D1,D0
        Moto68000 registerCpu(registers);
        boot(registerCpu);
        execute(registerCpu);
        execute(registerCpu);
        check(execute(registerCpu) == 8 && registerCpu.state().data[0] == 3 &&
                  (registerCpu.state().sr & 0x1f) == 0,
              "ADD.L entre Dn consome oito ciclos");
        check(execute(registerCpu) == 8 && registerCpu.state().data[0] == 1 &&
                  (registerCpu.state().sr & 0x1f) == 0,
              "SUB.L entre Dn consome oito ciclos");

        Bus add;
        add.setup();
        add.putWord(0x100, 0x707f); // MOVEQ #127,D0
        add.putWord(0x102, 0xd03c);
        add.putWord(0x104, 1); // ADD.B #1,D0
        add.putWord(0x106, 0xd03c);
        add.putWord(0x108, 0x80); // ADD.B #128,D0
        Moto68000 cpu(add);
        boot(cpu);
        execute(cpu);
        check(execute(cpu) == 8 && cpu.state().data[0] == 0x80 &&
                  (cpu.state().sr & 0x1f) == 0x0a,
              "ADD.B sinaliza overflow sem carry");
        check(execute(cpu) == 8 && cpu.state().data[0] == 0 &&
                  (cpu.state().sr & 0x1f) == 0x17,
              "ADD.B sinaliza carry, overflow, X e zero");

        Bus sub;
        sub.setup();
        sub.putWord(0x100, 0x7080); // MOVEQ #-128,D0
        sub.putWord(0x102, 0x903c);
        sub.putWord(0x104, 1); // SUB.B #1,D0
        sub.putWord(0x106, 0x903c);
        sub.putWord(0x108, 0x81); // SUB.B #129,D0
        Moto68000 subCpu(sub);
        boot(subCpu);
        execute(subCpu);
        check(execute(subCpu) == 8 && subCpu.state().data[0] == 0xffffff7f &&
                  (subCpu.state().sr & 0x1f) == 0x02,
              "SUB.B sinaliza overflow sem empréstimo");
        check(execute(subCpu) == 8 && subCpu.state().data[0] == 0xfffffffe &&
                  (subCpu.state().sr & 0x1f) == 0x1b,
              "SUB.B sinaliza empréstimo, overflow e X");

        Bus carry;
        carry.setup();
        carry.putWord(0x100, 0x70ff); // MOVEQ #-1,D0
        carry.putWord(0x102, 0xd03c);
        carry.putWord(0x104, 1); // ADD.B #1,D0
        carry.putWord(0x106, 0x903c);
        carry.putWord(0x108, 1); // SUB.B #1,D0
        Moto68000 carryCpu(carry);
        boot(carryCpu);
        execute(carryCpu);
        check(execute(carryCpu) == 8 && carryCpu.state().data[0] == 0xffffff00u &&
                  (carryCpu.state().sr & 0x1f) == 0x15,
              "ADD.B produz carry sem overflow");
        check(execute(carryCpu) == 8 && carryCpu.state().data[0] == 0xffffffffu &&
                  (carryCpu.state().sr & 0x1f) == 0x19,
              "SUB.B produz empréstimo sem overflow");

        Bus memoryFlags;
        memoryFlags.setup();
        memoryFlags.putWord(0x100, 0x41f8);
        memoryFlags.putWord(0x102, 0x1100);
        memoryFlags.putWord(0x104, 0x7001); // MOVEQ #1,D0
        memoryFlags.putWord(0x106, 0xd110); // ADD.B D0,(A0)
        memoryFlags.putWord(0x108, 0x9150); // SUB.W D0,(A0)
        memoryFlags.putWord(0x1100, 0x7f00);
        Moto68000 memoryFlagsCpu(memoryFlags);
        boot(memoryFlagsCpu);
        execute(memoryFlagsCpu);
        execute(memoryFlagsCpu);
        check(execute(memoryFlagsCpu) == 12 && memoryFlags.word(0x1100) == 0x8000 &&
                  (memoryFlagsCpu.state().sr & 0x1f) == 0x0a,
              "ADD.B em memória sinaliza overflow");
        check(execute(memoryFlagsCpu) == 12 && memoryFlags.word(0x1100) == 0x7fff &&
                  (memoryFlagsCpu.state().sr & 0x1f) == 0x02 &&
                  memoryFlagsCpu.state().data[0] == 1,
              "SUB.W em memória sinaliza overflow e preserva D0");

        Bus stack;
        stack.setup(0x1802);
        stack.putWord(0x100, 0x7001); // MOVEQ #1,D0
        stack.putWord(0x102, 0xd127); // ADD.B D0,-(A7)
        stack.putWord(0x104, 0x911f); // SUB.B D0,(A7)+
        stack.putWord(0x1800, 0x7f00);
        Moto68000 stackCpu(stack);
        boot(stackCpu);
        execute(stackCpu);
        check(execute(stackCpu) == 14 && stackCpu.state().address[7] == 0x1800 &&
                  stack.byte(0x1800) == 0x80 &&
                  (stackCpu.state().sr & 0x1f) == 0x0a,
              "ADD.B pré-decrementa A7 em dois bytes");
        check(execute(stackCpu) == 12 && stackCpu.state().address[7] == 0x1802 &&
                  stack.byte(0x1800) == 0x7f &&
                  (stackCpu.state().sr & 0x1f) == 0x02,
              "SUB.B pós-incrementa A7 em dois bytes");
    }

    void checkAddressVariants()
    {
        Bus bus;
        bus.setup();
        bus.putWord(0x100, 0x70ff); // MOVEQ #-1,D0
        bus.putWord(0x102, 0x41f8);
        bus.putWord(0x104, 0x1100); // LEA $1100.W,A0
        bus.putWord(0x106, 0xd0fc);
        bus.putWord(0x108, 0xffff); // ADDA.W #-1,A0
        bus.putWord(0x10a, 0x90fc);
        bus.putWord(0x10c, 0xffff); // SUBA.W #-1,A0
        bus.putWord(0x10e, 0xd1c0); // ADDA.L D0,A0
        bus.putWord(0x110, 0x91c0); // SUBA.L D0,A0
        bus.putWord(0x112, 0xd048); // ADD.W A0,D0
        bus.putWord(0x114, 0x9048); // SUB.W A0,D0
        Moto68000 cpu(bus);
        boot(cpu);
        execute(cpu);
        execute(cpu);
        const std::uint16_t previousSr = cpu.state().sr;
        check(execute(cpu) == 12 && cpu.state().address[0] == 0x10ff &&
                  cpu.state().sr == previousSr,
              "ADDA.W estende sinal e preserva SR");
        check(execute(cpu) == 12 && cpu.state().address[0] == 0x1100 &&
                  cpu.state().sr == previousSr,
              "SUBA.W estende sinal e preserva SR");
        check(execute(cpu) == 8 && cpu.state().address[0] == 0x10ff &&
                  cpu.state().sr == previousSr,
              "ADDA.L aceita Dn como origem");
        check(execute(cpu) == 8 && cpu.state().address[0] == 0x1100 &&
                  cpu.state().sr == previousSr,
              "SUBA.L aceita Dn como origem");
        check(execute(cpu) == 4 && cpu.state().data[0] == 0xffff10ffu &&
                  (cpu.state().sr & 0x1f) == 0x11,
              "ADD.W aceita An como origem");
        check(execute(cpu) == 4 && cpu.state().data[0] == 0xffffffffu &&
                  (cpu.state().sr & 0x1f) == 0x19,
              "SUB.W aceita An como origem");

        Bus memory;
        memory.setup();
        memory.putWord(0x100, 0x41f8);
        memory.putWord(0x102, 0x1100);
        memory.putWord(0x104, 0xd2d0); // ADDA.W (A0),A1
        memory.putWord(0x106, 0x93d0); // SUBA.L (A0),A1
        memory.putWord(0x1100, 0xffff);
        memory.putWord(0x1102, 1);
        Moto68000 memoryCpu(memory);
        boot(memoryCpu);
        execute(memoryCpu);
        check(execute(memoryCpu) == 12 && memoryCpu.state().address[1] == 0xffffffffu &&
                  (memoryCpu.state().sr & 0x1f) == 0,
              "ADDA.W lê memória e estende sinal");
        check(execute(memoryCpu) == 14 && memoryCpu.state().address[1] == 0xfffe &&
                  memoryCpu.state().address[0] == 0x1100 &&
                  (memoryCpu.state().sr & 0x1f) == 0,
              "SUBA.L lê memória sem alterar o SR");
    }

    void checkWaitAndFaults()
    {
        Bus wait;
        wait.setup();
        wait.putWord(0x100, 0x41f8);
        wait.putWord(0x102, 0x1100);
        wait.putWord(0x104, 0x7001); // MOVEQ #1,D0
        wait.putWord(0x106, 0xd190); // ADD.L D0,(A0)
        wait.putWord(0x1100, 0);
        wait.putWord(0x1102, 1);
        Moto68000 cpu(wait);
        boot(cpu);
        execute(cpu);
        execute(cpu);
        wait.transfers.clear();
        wait.waitAddress = 0x1102;
        wait.readWait = 2;
        wait.writeWait = 3;
        check(execute(cpu) == 25 && wait.word(0x1100) == 0 &&
                  wait.word(0x1102) == 2 && cpu.state().data[0] == 1 &&
                  wait.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x1102}, {'R', 0x10a}, {'W', 0x1102}, {'W', 0x1100}}),
              "ADD.L aguarda leitura e escrita sem repetir transferências");

        Bus odd;
        odd.setup();
        odd.putWord(0x100, 0x41f8);
        odd.putWord(0x102, 0x1101);
        odd.putWord(0x104, 0x9050); // SUB.W (A0),D0
        Moto68000 oddCpu(odd);
        boot(oddCpu);
        execute(oddCpu);
        runUntilPc(oddCpu, 0x200);
        check(oddCpu.state().instructions == 1 &&
                  odd.word(0x17f8) == 0x9050 && odd.word(0x17f6) == 0x1101 &&
                  odd.word(0x17fe) == 0x106 && odd.word(0x17f2) == 0x9055,
              "SUB.W desalinhado gera erro de endereço na leitura");

        Bus illegal;
        illegal.setup();
        illegal.putWord(0x100, 0xd008); // ADD.B A0,D0 é inválido.
        Moto68000 illegalCpu(illegal);
        boot(illegalCpu);
        runUntilPc(illegalCpu, 0x300);
        check(illegalCpu.state().instructions == 0 &&
                  illegal.word(0x17fe) == 0x100,
              "ADD.B com origem An usa vetor 4");

        Bus writeFault;
        writeFault.setup();
        writeFault.putWord(0x100, 0x41f8);
        writeFault.putWord(0x102, 0x1100);
        writeFault.putWord(0x104, 0x7001);
        writeFault.putWord(0x106, 0x9158); // SUB.W D0,(A0)+
        writeFault.putWord(0x1100, 3);
        Moto68000 writeFaultCpu(writeFault);
        boot(writeFaultCpu);
        execute(writeFaultCpu);
        execute(writeFaultCpu);
        writeFault.transfers.clear();
        writeFault.faultAddress = 0x1100;
        writeFault.faultWrite = true;
        runUntilPc(writeFaultCpu, 0x400);
        check(writeFaultCpu.state().instructions == 2 &&
                  writeFaultCpu.state().address[0] == 0x1102 &&
                  writeFault.word(0x1100) == 3 &&
                  writeFault.word(0x17f8) == 0x9158 &&
                  writeFault.word(0x17fc) == 0x1100 &&
                  writeFault.word(0x17fe) == 0x05 &&
                  writeFault.transfers.size() >= 2 &&
                  writeFault.transfers[0] == std::pair<char, std::uint32_t>('R', 0x1100) &&
                  writeFault.transfers[1] == std::pair<char, std::uint32_t>('R', 0x10a),
              "SUB.W com erro de escrita preserva memória e quadro de falha");
    }

    void checkImmediateArithmetic()
    {
        struct Addressing
        {
            const char *name;
            unsigned mode, reg, words, byteWordCycles, longCycles;
        };
        const std::array<Addressing, 8> modes = {{{"D0", 0, 0, 0, 0, 0},
                                                  {"(A0)", 2, 0, 0, 4, 8},
                                                  {"(A0)+", 3, 0, 0, 4, 8},
                                                  {"-(A0)", 4, 0, 0, 6, 10},
                                                  {"(4,A0)", 5, 0, 1, 8, 12},
                                                  {"(3,A0,D0.W)", 6, 0, 1, 10, 14},
                                                  {"$1100.W", 7, 0, 1, 8, 12},
                                                  {"$00001100.L", 7, 1, 2, 12, 16}}};

        for (unsigned subtract = 0; subtract < 2; ++subtract)
        {
            for (unsigned sizeCode = 0; sizeCode < 3; ++sizeCode)
            {
                const unsigned size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                                        : 4;
                for (const auto &ea : modes)
                {
                    Bus bus;
                    bus.setup();
                    bus.putWord(0x100, 0x41f8);
                    bus.putWord(0x102, 0x1100);
                    bus.putWord(0x104, 0x7001); // MOVEQ #1,D0
                    bus.putWord(0x106, (subtract ? 0x0400 : 0x0600) |
                                           (sizeCode << 6) | (ea.mode << 3) | ea.reg);
                    const unsigned immediateWords = size == 4 ? 2 : 1;
                    const unsigned extensions = immediateWords + ea.words;
                    bus.putWord(0x108, size == 4 ? 0 : 1);
                    if (size == 4)
                        bus.putWord(0x10a, 1);
                    const std::uint32_t eaExtension = 0x108 + immediateWords * 2;
                    if (ea.mode == 5)
                        bus.putWord(eaExtension, 4);
                    else if (ea.mode == 6)
                        bus.putWord(eaExtension, 3);
                    else if (ea.mode == 7 && ea.reg == 0)
                        bus.putWord(eaExtension, 0x1100);
                    else if (ea.mode == 7 && ea.reg == 1)
                    {
                        bus.putWord(eaExtension, 0);
                        bus.putWord(eaExtension + 2, 0x1100);
                    }

                    std::uint32_t target = 0x1100;
                    if (ea.mode == 4)
                        target -= size;
                    else if (ea.mode == 5 || ea.mode == 6)
                        target += 4;
                    if (ea.mode != 0)
                    {
                        bus.putWord(target, size == 1 ? 0x0100 : size == 2 ? 1
                                                                           : 0);
                        if (size == 4)
                            bus.putWord(target + 2, 1);
                    }

                    Moto68000 cpu(bus);
                    boot(cpu);
                    execute(cpu);
                    execute(cpu);
                    bus.transfers.clear();
                    const std::string label = std::string(subtract ? "SUBI." : "ADDI.") +
                                              (size == 1 ? "B " : size == 2 ? "W "
                                                                            : "L ") +
                                              ea.name;
                    const unsigned eaCycles = size == 4 ? ea.longCycles : ea.byteWordCycles;
                    const unsigned baseCycles = ea.mode == 0 ? (size == 4 ? 16 : 8) : (size == 4 ? 20 : 12);
                    check(execute(cpu) == baseCycles + eaCycles,
                          label + ": ciclos");
                    const std::uint32_t result = subtract ? 0 : 2;
                    check(cpu.state().pc == 0x108 + extensions * 2 &&
                              (cpu.state().sr & 0x1f) == (subtract ? 4 : 0),
                          label + ": PC ou flags");
                    const int step = ea.mode == 4 ? -static_cast<int>(size) : ea.mode == 3 ? static_cast<int>(size)
                                                                                           : 0;
                    check(cpu.state().address[0] == 0x1100 + step,
                          label + ": atualização de A0");
                    if (ea.mode == 0)
                        check(cpu.state().data[0] == result, label + ": D0");
                    else
                        check(cpu.state().data[0] == 1 &&
                                  (size == 1 ? bus.byte(target) == result : size == 2 ? bus.word(target) == result
                                                                                      : bus.word(target) == 0 && bus.word(target + 2) == result),
                              label + ": memória");

                    std::vector<std::pair<char, std::uint32_t>> expected;
                    for (unsigned word = 0; word < extensions; ++word)
                        expected.emplace_back('R', 0x10a + word * 2);
                    if (ea.mode != 0)
                    {
                        expected.emplace_back(size == 1 ? 'r' : 'R', target);
                        if (size == 4)
                            expected.emplace_back('R', target + 2);
                    }
                    expected.emplace_back('R', 0x10a + extensions * 2);
                    if (ea.mode != 0)
                    {
                        if (size == 4)
                            expected.emplace_back('W', target + 2);
                        expected.emplace_back(size == 1 ? 'w' : 'W', target);
                    }
                    check(bus.transfers == expected, label + ": ordem do barramento");
                }
            }
        }

        Bus flags;
        flags.setup();
        flags.putWord(0x100, 0x707f); // MOVEQ #127,D0
        flags.putWord(0x102, 0x0600);
        flags.putWord(0x104, 0xff01); // ADDI.B #1,D0
        flags.putWord(0x106, 0x0400);
        flags.putWord(0x108, 1); // SUBI.B #1,D0
        Moto68000 flagsCpu(flags);
        boot(flagsCpu);
        execute(flagsCpu);
        check(execute(flagsCpu) == 8 && flagsCpu.state().data[0] == 0x80 &&
                  (flagsCpu.state().sr & 0x1f) == 0x0a,
              "ADDI.B ignora byte alto do imediato e sinaliza overflow");
        check(execute(flagsCpu) == 8 && flagsCpu.state().data[0] == 0x7f &&
                  (flagsCpu.state().sr & 0x1f) == 0x02,
              "SUBI.B sinaliza overflow negativo");
    }

    void checkImmediateWaitAndFaults()
    {
        Bus stack;
        stack.setup(0x1802);
        stack.putWord(0x100, 0x0627);
        stack.putWord(0x102, 1); // ADDI.B #1,-(A7)
        stack.putWord(0x1800, 0x7f00);
        stack.waitAddress = 0x1800;
        stack.writeWait = 2;
        Moto68000 stackCpu(stack);
        boot(stackCpu);
        stack.transfers.clear();
        check(execute(stackCpu) == 20 && stackCpu.state().address[7] == 0x1800 &&
                  stack.byte(0x1800) == 0x80 &&
                  (stackCpu.state().sr & 0x1f) == 0x0a &&
                  stack.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x104}, {'r', 0x1800}, {'R', 0x106}, {'w', 0x1800}}),
              "ADDI.B espera escrita sem repetir o pré-decremento de A7");

        Bus longWait;
        longWait.setup();
        longWait.putWord(0x100, 0x41f8);
        longWait.putWord(0x102, 0x1100);
        longWait.putWord(0x104, 0x0490); // SUBI.L #1,(A0)
        longWait.putWord(0x106, 0);
        longWait.putWord(0x108, 1);
        longWait.putWord(0x1100, 0);
        longWait.putWord(0x1102, 1);
        Moto68000 longCpu(longWait);
        boot(longCpu);
        execute(longCpu);
        longWait.transfers.clear();
        longWait.waitAddress = 0x1102;
        longWait.readWait = 2;
        longWait.writeWait = 3;
        check(execute(longCpu) == 33 && longWait.word(0x1100) == 0 &&
                  longWait.word(0x1102) == 0 &&
                  (longCpu.state().sr & 0x1f) == 4 &&
                  longWait.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x108}, {'R', 0x10a}, {'R', 0x1100}, {'R', 0x1102}, {'R', 0x10c}, {'W', 0x1102}, {'W', 0x1100}}),
              "SUBI.L mantém a ordem das buscas e espera nas segundas metades");

        Bus odd;
        odd.setup();
        odd.putWord(0x100, 0x41f8);
        odd.putWord(0x102, 0x1101);
        odd.putWord(0x104, 0x0650);
        odd.putWord(0x106, 1); // ADDI.W #1,(A0)
        Moto68000 oddCpu(odd);
        boot(oddCpu);
        execute(oddCpu);
        runUntilPc(oddCpu, 0x200);
        check(oddCpu.state().instructions == 1 &&
                  odd.word(0x17f8) == 0x0650 && odd.word(0x17f6) == 0x1101 &&
                  odd.word(0x17fe) == 0x108 && odd.word(0x17f2) == 0x0655,
              "ADDI.W desalinhado gera erro de endereço na leitura");

        Bus illegal;
        illegal.setup();
        illegal.putWord(0x100, 0x0648);
        illegal.putWord(0x102, 1); // ADDI.W #1,A0
        Moto68000 illegalCpu(illegal);
        boot(illegalCpu);
        runUntilPc(illegalCpu, 0x300);
        check(illegalCpu.state().instructions == 0 &&
                  illegal.word(0x17fe) == 0x100,
              "ADDI.W com destino An usa vetor 4");

        Bus writeFault;
        writeFault.setup();
        writeFault.putWord(0x100, 0x41f8);
        writeFault.putWord(0x102, 0x1100);
        writeFault.putWord(0x104, 0x0458);
        writeFault.putWord(0x106, 1);
        writeFault.putWord(0x1100, 3); // SUBI.W #1,(A0)+
        Moto68000 faultCpu(writeFault);
        boot(faultCpu);
        execute(faultCpu);
        writeFault.faultAddress = 0x1100;
        writeFault.faultWrite = true;
        runUntilPc(faultCpu, 0x400);
        check(faultCpu.state().instructions == 1 &&
                  faultCpu.state().address[0] == 0x1102 &&
                  writeFault.word(0x1100) == 3 &&
                  writeFault.word(0x17f8) == 0x0458 &&
                  writeFault.word(0x17fc) == 0x1100 &&
                  writeFault.word(0x17fe) == 0x05,
              "SUBI.W com falha na escrita preserva memória e quadro de exceção");
    }
}

int main()
{
    try
    {
        checkAddressingMatrix();
        checkRegisterFlags();
        checkAddressVariants();
        checkWaitAndFaults();
        checkImmediateArithmetic();
        checkImmediateWaitAndFaults();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
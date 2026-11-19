#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <string>
#include <stdexcept>
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
        std::uint32_t waitAddress = 0xffffffff;
        unsigned readWait = 0, writeWait = 0;

        void putWord(std::uint32_t address, std::uint16_t value)
        {
            bytes[address] = static_cast<std::uint8_t>(value >> 8);
            bytes[address + 1] = static_cast<std::uint8_t>(value);
        }
        std::uint8_t byte(std::uint32_t address) const
        {
            return bytes.count(address) ? bytes.at(address) : 0;
        }
        std::uint8_t readByte(std::uint32_t address) override
        {
            transfers.emplace_back('r', address);
            return byte(address);
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            transfers.emplace_back('R', address);
            if (!bytes.count(address) && !bytes.count(address + 1))
                return 0x4e71;
            return (std::uint16_t(byte(address)) << 8) | byte(address + 1);
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
            if (address == waitAddress && writeWait)
            {
                --writeWait;
                return false;
            }
            writeByte(address, value);
            return true;
        }

        void setup(std::uint16_t stack = 0x1800)
        {
            putWord(0, 0);
            putWord(2, stack);
            putWord(4, 0);
            putWord(6, 0x0100);
            for (unsigned address = 0x100; address < 0x120; address += 2)
                putWord(address, 0x4e71);
        }
    };

    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void run(Moto68000 &cpu, unsigned cycles)
    {
        while (cycles--)
            cpu.clock();
    }
}

int main()
{
    Bus sameRegister;
    sameRegister.setup();
    sameRegister.putWord(0x100, 0x41f8);
    sameRegister.putWord(0x102, 0x1100);
    sameRegister.putWord(0x104, 0x20d8); // MOVE.L (A0)+,(A0)+
    sameRegister.putWord(0x1100, 0x1234);
    sameRegister.putWord(0x1102, 0x5678);
    Moto68000 sameRegisterCpu(sameRegister);
    sameRegisterCpu.reset();
    run(sameRegisterCpu, 40 + 8 + 20);
    check(sameRegisterCpu.state().pc == 0x106 &&
              sameRegisterCpu.state().address[0] == 0x1108 &&
              sameRegister.byte(0x1104) == 0x12 && sameRegister.byte(0x1105) == 0x34 &&
              sameRegister.byte(0x1106) == 0x56 && sameRegister.byte(0x1107) == 0x78,
          "origem e destino compartilham A0 após pós-incremento");

    Bus stack;
    stack.setup(0x1804);
    stack.putWord(0x100, 0x1f27); // MOVE.B -(A7),-(A7)
    stack.bytes[0x1802] = 0x80;
    Moto68000 stackCpu(stack);
    stackCpu.reset();
    run(stackCpu, 40 + 14);
    check(stackCpu.state().address[7] == 0x1800 && stack.byte(0x1800) == 0x80 &&
              (stackCpu.state().sr & 0x0f) == 8,
          "origem e destino pré-decrementam A7 em dois bytes");

    Bus displacedToAbsolute;
    displacedToAbsolute.setup();
    displacedToAbsolute.putWord(0x100, 0x41f8);
    displacedToAbsolute.putWord(0x102, 0x1100);
    displacedToAbsolute.putWord(0x104, 0x23e8); // MOVE.L (4,A0),($00001200).L
    displacedToAbsolute.putWord(0x106, 4);
    displacedToAbsolute.putWord(0x108, 0);
    displacedToAbsolute.putWord(0x10a, 0x1200);
    displacedToAbsolute.putWord(0x1104, 0x1234);
    displacedToAbsolute.putWord(0x1106, 0x5678);
    Moto68000 displacedToAbsoluteCpu(displacedToAbsolute);
    displacedToAbsoluteCpu.reset();
    run(displacedToAbsoluteCpu, 40 + 8);
    displacedToAbsolute.transfers.clear();
    run(displacedToAbsoluteCpu, 32);
    check(displacedToAbsoluteCpu.state().pc == 0x10c &&
              displacedToAbsolute.byte(0x1200) == 0x12 &&
              displacedToAbsolute.byte(0x1203) == 0x78,
          "MOVE.L combina deslocamento e absoluto longo");
    check(displacedToAbsolute.transfers ==
              std::vector<std::pair<char, std::uint32_t>>({{'R', 0x108}, {'R', 0x1104}, {'R', 0x1106}, {'R', 0x10a}, {'W', 0x1200}, {'W', 0x1202}, {'R', 0x10c}, {'R', 0x10e}}),
          "absoluto longo após origem em memória preserva a ordem do barramento");

    Bus immediateLong;
    immediateLong.setup();
    immediateLong.putWord(0x100, 0x23fc); // MOVE.L #$12345678,($00001300).L
    immediateLong.putWord(0x102, 0x1234);
    immediateLong.putWord(0x104, 0x5678);
    immediateLong.putWord(0x106, 0);
    immediateLong.putWord(0x108, 0x1300);
    Moto68000 immediateLongCpu(immediateLong);
    immediateLongCpu.reset();
    run(immediateLongCpu, 40);
    immediateLong.transfers.clear();
    run(immediateLongCpu, 28);
    check(immediateLongCpu.state().pc == 0x10a &&
              immediateLong.byte(0x1300) == 0x12 && immediateLong.byte(0x1301) == 0x34 &&
              immediateLong.byte(0x1302) == 0x56 && immediateLong.byte(0x1303) == 0x78,
          "MOVE.L imediato para absoluto longo");
    check(immediateLong.transfers ==
              std::vector<std::pair<char, std::uint32_t>>({{'R', 0x104}, {'R', 0x106}, {'R', 0x108}, {'R', 0x10a}, {'W', 0x1300}, {'W', 0x1302}, {'R', 0x10c}}),
          "extensões imediatas precedem as do destino");

    Bus immediateDisplacement;
    immediateDisplacement.setup();
    immediateDisplacement.putWord(0x100, 0x41f8);
    immediateDisplacement.putWord(0x102, 0x1104);
    immediateDisplacement.putWord(0x104, 0x317c); // MOVE.W #$8001,(-4,A0)
    immediateDisplacement.putWord(0x106, 0x8001);
    immediateDisplacement.putWord(0x108, 0xfffc);
    Moto68000 immediateDisplacementCpu(immediateDisplacement);
    immediateDisplacementCpu.reset();
    run(immediateDisplacementCpu, 40 + 8 + 16);
    check(immediateDisplacementCpu.state().pc == 0x10a &&
              immediateDisplacementCpu.state().address[0] == 0x1104 &&
              immediateDisplacement.byte(0x1100) == 0x80 &&
              immediateDisplacement.byte(0x1101) == 0x01 &&
              (immediateDisplacementCpu.state().sr & 0x0f) == 8,
          "MOVE.W imediato com deslocamento negativo");

    Bus immediateStack;
    immediateStack.setup(0x1802);
    immediateStack.putWord(0x100, 0x1f3c); // MOVE.B #$80,-(A7)
    immediateStack.putWord(0x102, 0x0080);
    immediateStack.waitAddress = 0x1800;
    immediateStack.writeWait = 2;
    Moto68000 immediateStackCpu(immediateStack);
    immediateStackCpu.reset();
    run(immediateStackCpu, 40 + 12);
    check(immediateStackCpu.state().pc == 0x100 &&
              immediateStackCpu.state().address[7] == 0x1800 &&
              immediateStackCpu.pins().waiting && immediateStack.byte(0x1800) == 0,
          "espera na escrita imediata mantém o pré-decremento único");
    run(immediateStackCpu, 2);
    check(immediateStackCpu.state().pc == 0x104 &&
              immediateStackCpu.state().address[7] == 0x1800 &&
              immediateStack.byte(0x1800) == 0x80,
          "MOVE.B imediato conclui após a espera");

    Bus longStack;
    longStack.setup(0x1804);
    longStack.putWord(0x100, 0x2f3c); // MOVE.L #$12345678,-(A7)
    longStack.putWord(0x102, 0x1234);
    longStack.putWord(0x104, 0x5678);
    Moto68000 longStackCpu(longStack);
    longStackCpu.reset();
    run(longStackCpu, 40);
    longStack.transfers.clear();
    run(longStackCpu, 20);
    check(longStackCpu.state().address[7] == 0x1800 &&
              longStack.byte(0x1800) == 0x12 && longStack.byte(0x1801) == 0x34 &&
              longStack.byte(0x1802) == 0x56 && longStack.byte(0x1803) == 0x78,
          "MOVE.L imediato grava valor completo em pré-decremento");
    check(longStack.transfers ==
              std::vector<std::pair<char, std::uint32_t>>({{'R', 0x104}, {'R', 0x106}, {'R', 0x108}, {'W', 0x1802}, {'W', 0x1800}}),
          "pré-decremento longo antecipa a pré-busca e grava a palavra baixa primeiro");

    Bus sharedStack;
    sharedStack.setup(0x1800);
    sharedStack.putWord(0x100, 0x1edf); // MOVE.B (A7)+,(A7)+
    sharedStack.bytes[0x1800] = 0x7e;
    Moto68000 sharedStackCpu(sharedStack);
    sharedStackCpu.reset();
    run(sharedStackCpu, 40 + 12);
    check(sharedStackCpu.state().address[7] == 0x1804 &&
              sharedStack.byte(0x1802) == 0x7e,
          "dois pós-incrementos de byte em A7 avançam quatro bytes");

    Bus waitedSource;
    waitedSource.setup();
    waitedSource.putWord(0x100, 0x41f8);
    waitedSource.putWord(0x102, 0x1100);
    waitedSource.putWord(0x104, 0x43f8);
    waitedSource.putWord(0x106, 0x1200);
    waitedSource.putWord(0x108, 0x32e0); // MOVE.W -(A0),(A1)+
    waitedSource.putWord(0x10fe, 0x8001);
    waitedSource.waitAddress = 0x10fe;
    waitedSource.readWait = 2;
    Moto68000 waitedSourceCpu(waitedSource);
    waitedSourceCpu.reset();
    run(waitedSourceCpu, 40 + 16 + 6);
    check(waitedSourceCpu.state().pc == 0x108 &&
              waitedSourceCpu.state().address[0] == 0x10fe &&
              waitedSourceCpu.state().address[1] == 0x1200 &&
              waitedSourceCpu.pins().waiting,
          "espera na origem mantém destino intacto e pré-decrementa uma vez");
    run(waitedSourceCpu, 10);
    check(waitedSourceCpu.state().pc == 0x10a &&
              waitedSourceCpu.state().address[0] == 0x10fe &&
              waitedSourceCpu.state().address[1] == 0x1202 &&
              waitedSource.byte(0x1200) == 0x80 && waitedSource.byte(0x1201) == 0x01,
          "MOVE de memória para memória conclui após a espera");

    Bus selfModified;
    selfModified.setup();
    selfModified.putWord(0x100, 0x31fc); // MOVE.W #$4E71,($0108).W
    selfModified.putWord(0x102, 0x4e71);
    selfModified.putWord(0x104, 0x0108);
    selfModified.putWord(0x106, 0x7001);
    selfModified.putWord(0x108, 0x7002);
    Moto68000 selfModifiedCpu(selfModified);
    selfModifiedCpu.reset();
    run(selfModifiedCpu, 40 + 16);
    check(selfModifiedCpu.state().prefetch[0] == 0x7001 &&
              selfModifiedCpu.state().prefetch[1] == 0x4e71 &&
              selfModified.byte(0x108) == 0x4e,
          "última pré-busca observa a palavra gravada pelo MOVE");

    Bus registerToAbsolute;
    registerToAbsolute.setup();
    registerToAbsolute.putWord(0x100, 0x303c); // MOVE.W #$4E71,D0
    registerToAbsolute.putWord(0x102, 0x4e71);
    registerToAbsolute.putWord(0x104, 0x31c0); // MOVE.W D0,($010A).W
    registerToAbsolute.putWord(0x106, 0x010a);
    registerToAbsolute.putWord(0x108, 0x7001);
    registerToAbsolute.putWord(0x10a, 0x7002);
    Moto68000 registerToAbsoluteCpu(registerToAbsolute);
    registerToAbsoluteCpu.reset();
    run(registerToAbsoluteCpu, 40 + 8 + 12);
    check(registerToAbsoluteCpu.state().prefetch[0] == 0x7001 &&
              registerToAbsoluteCpu.state().prefetch[1] == 0x4e71,
          "MOVE de Dn para absoluto busca a palavra seguinte após a escrita");

    // Cruza os modos de origem e destino já implementados nos três tamanhos.
    const std::array<unsigned, 7> sources = {2, 3, 4, 5, 70, 71, 74};
    const std::array<unsigned, 6> targets = {2, 3, 4, 5, 70, 71};
    for (const unsigned size : {1u, 2u, 4u})
    {
        const std::uint32_t value = size == 1 ? 0x80 : size == 2 ? 0x8001
                                                                 : 0x80000001;
        for (const unsigned source : sources)
        {
            for (const unsigned target : targets)
            {
                Bus bus;
                bus.setup();
                bus.putWord(0x100, 0x41f8);
                bus.putWord(0x102, 0x1100);
                bus.putWord(0x104, 0x43f8);
                bus.putWord(0x106, 0x1200);
                const unsigned sourceMode = source < 10 ? source : 7;
                const unsigned sourceReg = source == 71 ? 1 : source == 74 ? 4
                                                                           : 0;
                const unsigned targetMode = target < 10 ? target : 7;
                const unsigned targetReg = target < 10 || target == 71 ? 1 : 0;
                const unsigned sourceExtensions = source == 5 || source == 70 ? 1 : source == 71 || (source == 74 && size == 4) ? 2
                                                                                : source == 74                                  ? 1
                                                                                                                                : 0;
                const unsigned targetExtensions = target == 5 || target == 70 ? 1 : target == 71 ? 2
                                                                                                 : 0;
                const unsigned prefix = size == 1 ? 1 : size == 2 ? 3
                                                                  : 2;
                const std::uint16_t opcode = static_cast<std::uint16_t>(
                    (prefix << 12) | (targetReg << 9) | (targetMode << 6) |
                    (sourceMode << 3) | sourceReg);
                bus.putWord(0x108, opcode);
                unsigned extensionAddress = 0x10a;
                if (source == 5)
                {
                    bus.putWord(extensionAddress, 4);
                    extensionAddress += 2;
                }
                else if (source == 70)
                {
                    bus.putWord(extensionAddress, 0x1100);
                    extensionAddress += 2;
                }
                else if (source == 71)
                {
                    bus.putWord(extensionAddress, 0);
                    bus.putWord(extensionAddress + 2, 0x1100);
                    extensionAddress += 4;
                }
                else if (source == 74)
                {
                    if (size == 4)
                    {
                        bus.putWord(extensionAddress, static_cast<std::uint16_t>(value >> 16));
                        extensionAddress += 2;
                    }
                    bus.putWord(extensionAddress, static_cast<std::uint16_t>(value));
                    extensionAddress += 2;
                }
                if (target == 5)
                {
                    bus.putWord(extensionAddress, 4);
                    extensionAddress += 2;
                }
                else if (target == 70)
                {
                    bus.putWord(extensionAddress, 0x1200);
                    extensionAddress += 2;
                }
                else if (target == 71)
                {
                    bus.putWord(extensionAddress, 0);
                    bus.putWord(extensionAddress + 2, 0x1200);
                    extensionAddress += 4;
                }
                const unsigned sourceStep = size;
                const unsigned sourceAddress = source == 4 ? 0x1100 - sourceStep : source == 5 ? 0x1104
                                                                                               : 0x1100;
                const unsigned targetAddress = target == 4 ? 0x1200 - size : target == 5 ? 0x1204
                                                                                         : 0x1200;
                if (source != 74)
                {
                    if (size == 1)
                        bus.bytes[sourceAddress] = static_cast<std::uint8_t>(value);
                    else if (size == 2)
                        bus.putWord(sourceAddress, static_cast<std::uint16_t>(value));
                    else
                    {
                        bus.putWord(sourceAddress, static_cast<std::uint16_t>(value >> 16));
                        bus.putWord(sourceAddress + 2, static_cast<std::uint16_t>(value));
                    }
                }
                Moto68000 cpu(bus);
                cpu.reset();
                const unsigned transferWords = size == 4 ? 2 : 1;
                const unsigned cycles = 4 * (sourceExtensions + targetExtensions +
                                             (source == 74 ? 0 : transferWords) + transferWords + 1) +
                                        (source == 4 ? 2 : 0);
                run(cpu, 40 + 16 + cycles);
                const unsigned nextPc = 0x10a + 2 * (sourceExtensions + targetExtensions);
                const unsigned nextA0 = source == 3 ? 0x1100 + size : source == 4 ? 0x1100 - size
                                                                                  : 0x1100;
                const unsigned nextA1 = target == 3 ? 0x1200 + size : target == 4 ? 0x1200 - size
                                                                                  : 0x1200;
                bool copied = true;
                for (unsigned byte = 0; byte < size; ++byte)
                    copied &= bus.byte(targetAddress + byte) ==
                              static_cast<std::uint8_t>(value >> ((size - byte - 1) * 8));
                if (!copied || cpu.state().pc != nextPc ||
                    cpu.state().address[0] != nextA0 ||
                    cpu.state().address[1] != nextA1 ||
                    cpu.state().instructions != 3 ||
                    (cpu.state().sr & 0x0f) != 8)
                    throw std::runtime_error("Falha na matriz MOVE: tamanho=" +
                                             std::to_string(size) + " origem=" + std::to_string(source) +
                                             " destino=" + std::to_string(target));
            }
        }
    }
}
#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
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
        std::vector<std::uint32_t> reads, writes;

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
            reads.push_back(address);
            return byte(address);
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            reads.push_back(address);
            if (!bytes.count(address) && !bytes.count(address + 1))
                return 0x4e71;
            return (std::uint16_t(byte(address)) << 8) | byte(address + 1);
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            writes.push_back(address);
            bytes[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            writes.push_back(address);
            putWord(address, value);
        }
        void setup()
        {
            putWord(0, 0);
            putWord(2, 0x1800);
            putWord(4, 0);
            putWord(6, 0x0100);
            for (unsigned address = 0x100; address < 0x130; address += 2)
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
    for (const unsigned size : {1u, 2u, 4u})
    {
        Bus bus;
        bus.setup();
        bus.putWord(0x100, 0x203c); // MOVE.L #$12348001,D0
        bus.putWord(0x102, 0x1234);
        bus.putWord(0x104, 0x8001);
        bus.putWord(0x106, 0x223c); // MOVE.L #$AABBCCDD,D1
        bus.putWord(0x108, 0xaabb);
        bus.putWord(0x10a, 0xccdd);
        bus.putWord(0x10c, size == 1 ? 0x1200 : size == 2 ? 0x3200
                                                          : 0x2200); // MOVE D0,D1
        Moto68000 cpu(bus);
        cpu.reset();
        run(cpu, 40 + 12 + 12 + 4);
        const std::uint32_t expected = size == 1 ? 0xaabbcc01 : size == 2 ? 0xaabb8001
                                                                          : 0x12348001;
        check(cpu.state().pc == 0x10e && cpu.state().data[1] == expected &&
                  (cpu.state().sr & 0x0f) == (size == 2 ? 8 : 0) &&
                  bus.reads.back() == 0x110 && bus.writes.empty(),
              "MOVE entre registradores Dn respeita tamanho e ciclos");
    }

    Bus addressSource;
    addressSource.setup();
    addressSource.putWord(0x100, 0x41f8); // LEA $FF80.W,A0
    addressSource.putWord(0x102, 0xff80);
    addressSource.putWord(0x104, 0x223c); // MOVE.L #$AABBCCDD,D1
    addressSource.putWord(0x106, 0xaabb);
    addressSource.putWord(0x108, 0xccdd);
    addressSource.putWord(0x10a, 0x3208); // MOVE.W A0,D1
    addressSource.putWord(0x10c, 0x2208); // MOVE.L A0,D1
    Moto68000 addressSourceCpu(addressSource);
    addressSourceCpu.reset();
    run(addressSourceCpu, 40 + 8 + 12 + 4);
    check(addressSourceCpu.state().data[1] == 0xaabbff80 &&
              (addressSourceCpu.state().sr & 0x0f) == 8,
          "MOVE.W de An para Dn preserva metade alta");
    run(addressSourceCpu, 4);
    check(addressSourceCpu.state().data[1] == 0xffffff80 &&
              (addressSourceCpu.state().sr & 0x0f) == 8,
          "MOVE.L de An para Dn copia os 32 bits");

    Bus wordAddress;
    wordAddress.setup();
    wordAddress.putWord(0x100, 0x203c); // MOVE.L #$12348001,D0
    wordAddress.putWord(0x102, 0x1234);
    wordAddress.putWord(0x104, 0x8001);
    wordAddress.putWord(0x106, 0x7400); // MOVEQ #0,D2: Z=1
    wordAddress.putWord(0x108, 0x3240); // MOVEA.W D0,A1
    Moto68000 wordAddressCpu(wordAddress);
    wordAddressCpu.reset();
    run(wordAddressCpu, 40 + 12 + 4 + 4);
    check(wordAddressCpu.state().address[1] == 0xffff8001 &&
              (wordAddressCpu.state().sr & 0x1f) == 4 &&
              wordAddressCpu.state().pc == 0x10a,
          "MOVEA.W estende sinal e preserva CCR");

    Bus longAddress;
    longAddress.setup();
    longAddress.putWord(0x100, 0x41f8);
    longAddress.putWord(0x102, 0xff80);
    longAddress.putWord(0x104, 0x7400);
    longAddress.putWord(0x106, 0x2248); // MOVEA.L A0,A1
    Moto68000 longAddressCpu(longAddress);
    longAddressCpu.reset();
    run(longAddressCpu, 40 + 8 + 4 + 4);
    check(longAddressCpu.state().address[0] == 0xffffff80 &&
              longAddressCpu.state().address[1] == 0xffffff80 &&
              (longAddressCpu.state().sr & 0x1f) == 4,
          "MOVEA.L entre registradores de endereço preserva CCR");

    Bus immediateAddress;
    immediateAddress.setup();
    immediateAddress.putWord(0x100, 0x7400);
    immediateAddress.putWord(0x102, 0x327c); // MOVEA.W #$8001,A1
    immediateAddress.putWord(0x104, 0x8001);
    immediateAddress.putWord(0x106, 0x227c); // MOVEA.L #$12345678,A1
    immediateAddress.putWord(0x108, 0x1234);
    immediateAddress.putWord(0x10a, 0x5678);
    Moto68000 immediateAddressCpu(immediateAddress);
    immediateAddressCpu.reset();
    run(immediateAddressCpu, 40 + 4 + 8);
    check(immediateAddressCpu.state().address[1] == 0xffff8001 &&
              (immediateAddressCpu.state().sr & 0x1f) == 4,
          "MOVEA.W imediato preserva CCR");
    run(immediateAddressCpu, 12);
    check(immediateAddressCpu.state().address[1] == 0x12345678 &&
              immediateAddressCpu.state().pc == 0x10c &&
              (immediateAddressCpu.state().sr & 0x1f) == 4,
          "MOVEA.L imediato consome duas extensões");

    Bus memoryAddress;
    memoryAddress.setup();
    memoryAddress.putWord(0x100, 0x41f8);
    memoryAddress.putWord(0x102, 0x1100);
    memoryAddress.putWord(0x104, 0x7400);
    memoryAddress.putWord(0x106, 0x3258); // MOVEA.W (A0)+,A1
    memoryAddress.putWord(0x108, 0x2260); // MOVEA.L -(A0),A1
    memoryAddress.putWord(0x1100, 0x8001);
    memoryAddress.putWord(0x10fe, 0x1234);
    Moto68000 memoryAddressCpu(memoryAddress);
    memoryAddressCpu.reset();
    run(memoryAddressCpu, 40 + 8 + 4 + 8);
    check(memoryAddressCpu.state().address[0] == 0x1102 &&
              memoryAddressCpu.state().address[1] == 0xffff8001 &&
              (memoryAddressCpu.state().sr & 0x1f) == 4,
          "MOVEA.W lê memória e aplica pós-incremento");
    memoryAddress.putWord(0x10fe, 0x1234);
    run(memoryAddressCpu, 14);
    check(memoryAddressCpu.state().address[0] == 0x10fe &&
              memoryAddressCpu.state().address[1] == 0x12348001 &&
              (memoryAddressCpu.state().sr & 0x1f) == 4,
          "MOVEA.L lê duas palavras após pré-decremento");

    Bus addressToMemory;
    addressToMemory.setup();
    addressToMemory.putWord(0x100, 0x41f8);
    addressToMemory.putWord(0x102, 0xff80);
    addressToMemory.putWord(0x104, 0x43f8);
    addressToMemory.putWord(0x106, 0x1100);
    addressToMemory.putWord(0x108, 0x22c8); // MOVE.L A0,(A1)+
    Moto68000 addressToMemoryCpu(addressToMemory);
    addressToMemoryCpu.reset();
    run(addressToMemoryCpu, 40 + 8 + 8 + 12);
    check(addressToMemoryCpu.state().address[1] == 0x1104 &&
              addressToMemory.byte(0x1100) == 0xff &&
              addressToMemory.byte(0x1101) == 0xff &&
              addressToMemory.byte(0x1102) == 0xff &&
              addressToMemory.byte(0x1103) == 0x80 &&
              (addressToMemoryCpu.state().sr & 0x0f) == 8,
          "MOVE.L de An para memória usa o mesmo caminho de origem");

    Bus stackAddress;
    stackAddress.setup();
    stackAddress.putWord(0x100, 0x3e7c); // MOVEA.W #$1802,A7
    stackAddress.putWord(0x102, 0x1802);
    stackAddress.putWord(0x104, 0x707f);
    stackAddress.putWord(0x106, 0x1f00); // MOVE.B D0,-(A7)
    Moto68000 stackAddressCpu(stackAddress);
    stackAddressCpu.reset();
    run(stackAddressCpu, 40 + 8 + 4 + 8);
    check(stackAddressCpu.state().address[7] == 0x1800 &&
              stackAddress.byte(0x1800) == 0x7f,
          "MOVEA.W para A7 integra-se ao acesso à pilha");

    // Exercita as fontes de MOVEA com valor, ciclos e CCR observáveis.
    const std::array<unsigned, 9> sources = {0, 1, 2, 3, 4, 5, 70, 71, 74};
    for (const unsigned size : {2u, 4u})
    {
        for (const unsigned source : sources)
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, 0x41f8);
            bus.putWord(0x102, 0x1100);
            bus.putWord(0x104, 0x203c); // MOVE.L #$80008001,D0
            bus.putWord(0x106, 0x8000);
            bus.putWord(0x108, 0x8001);
            bus.putWord(0x10a, 0x7400); // MOVEQ #0,D2: Z=1
            const unsigned sourceMode = source < 10 ? source : 7;
            const unsigned sourceReg = source == 71 ? 1 : source == 74 ? 4
                                                                       : 0;
            const unsigned prefix = size == 2 ? 3 : 2;
            bus.putWord(0x10c, static_cast<std::uint16_t>(
                                   (prefix << 12) | (1 << 9) | (1 << 6) |
                                   (sourceMode << 3) | sourceReg));
            unsigned extensionAddress = 0x10e;
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
                    bus.putWord(extensionAddress, 0x8000);
                    extensionAddress += 2;
                }
                bus.putWord(extensionAddress, 0x8001);
                extensionAddress += 2;
            }
            const unsigned sourceAddress = source == 4 ? 0x1100 - size : source == 5 ? 0x1104
                                                                                     : 0x1100;
            if (source >= 2 && source != 74)
            {
                if (size == 2)
                    bus.putWord(sourceAddress, 0x8001);
                else
                {
                    bus.putWord(sourceAddress, 0x8000);
                    bus.putWord(sourceAddress + 2, 0x8001);
                }
            }
            Moto68000 cpu(bus);
            cpu.reset();
            const unsigned extensions = (source == 5 || source == 70 ||
                                         (source == 74 && size == 2))
                                            ? 1
                                        : source == 71 || source == 74 ? 2
                                                                       : 0;
            const unsigned dataReads = source == 0 || source == 1 || source == 74 ? 0 : size == 4 ? 2
                                                                                                  : 1;
            const unsigned cycles = 4 * (extensions + dataReads + 1) +
                                    (source == 4 ? 2 : 0);
            run(cpu, 40 + 8 + 12 + 4 + cycles);
            const std::uint32_t sourceValue = source == 1 ? 0x1100 : source == 0 ? 0x80008001
                                                                 : size == 2     ? 0x8001
                                                                                 : 0x80008001;
            const std::uint32_t expected = size == 2 ? static_cast<std::uint32_t>(
                                                           static_cast<std::int32_t>(static_cast<std::int16_t>(sourceValue)))
                                                     : sourceValue;
            const unsigned nextA0 = source == 3 ? 0x1100 + size : source == 4 ? 0x1100 - size
                                                                              : 0x1100;
            if (cpu.state().address[1] != expected ||
                cpu.state().address[0] != nextA0 ||
                cpu.state().pc != 0x10e + 2 * extensions ||
                cpu.state().instructions != 4 ||
                (cpu.state().sr & 0x1f) != 4)
                throw std::runtime_error("Falha em MOVEA: tamanho=" +
                                         std::to_string(size) + " origem=" + std::to_string(source));
        }
    }

    for (const std::uint16_t invalid : {std::uint16_t(0x1008), std::uint16_t(0x1040)})
    {
        Bus bus;
        bus.setup();
        bus.putWord(0x100, invalid);
        Moto68000 cpu(bus);
        cpu.reset();
        run(cpu, 40);
        bool rejected = false;
        try
        {
            cpu.clock();
        }
        catch (const std::logic_error &)
        {
            rejected = true;
        }
        check(rejected, "MOVE.B com registrador de endereço deve ser recusado");
    }
}
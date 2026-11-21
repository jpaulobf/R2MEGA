#include "Core/CPU/Moto68000/Moto68000.h"
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    struct Bus : M68KBus
    {
        std::unordered_map<std::uint32_t, std::uint8_t> bytes;
        std::vector<M68KPins> completed;
        std::uint32_t waitAddress = 0xffffffff;
        unsigned readWait = 0;

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
            return byte(address);
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            if (!bytes.count(address) && !bytes.count(address + 1))
                return 0x4e71;
            return (std::uint16_t(byte(address)) << 8) | byte(address + 1);
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
            bytes[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            putWord(address, value);
        }
        void observe(const M68KPins &pins) override
        {
            if (pins.active && pins.phase == 3)
                completed.push_back(pins);
        }
        void setup()
        {
            putWord(0, 0);
            putWord(2, 0x1800);
            putWord(4, 0);
            putWord(6, 0x0100);
            for (unsigned address = 0x100; address < 0x160; address += 2)
                putWord(address, 0x4e71);
        }
        bool saw(std::uint32_t address, unsigned functionCode) const
        {
            for (const auto &pins : completed)
                if (pins.address == address && pins.functionCode == functionCode &&
                    !pins.write)
                    return true;
            return false;
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
    Bus indexedTarget;
    indexedTarget.setup();
    indexedTarget.putWord(0x100, 0x43f8); // LEA $1104.W,A1.
    indexedTarget.putWord(0x102, 0x1104);
    indexedTarget.putWord(0x104, 0x7004); // MOVEQ #4,D0.
    indexedTarget.putWord(0x106, 0x7e80); // MOVEQ #-128,D7.
    indexedTarget.putWord(0x108, 0x3387); // MOVE.W D7,(-8,A1,D0.W).
    indexedTarget.putWord(0x10a, 0x00f8);
    Moto68000 indexedTargetCpu(indexedTarget);
    indexedTargetCpu.reset();
    run(indexedTargetCpu, 40 + 8 + 4 + 4 + 14);
    check(indexedTargetCpu.state().pc == 0x10c &&
              indexedTarget.byte(0x1100) == 0xff && indexedTarget.byte(0x1101) == 0x80 &&
              (indexedTargetCpu.state().sr & 0x0f) == 8,
          "MOVE.W para destino indexado grava a palavra e atualiza os sinalizadores em 14 ciclos");

    Bus immediateTarget;
    immediateTarget.setup();
    immediateTarget.putWord(0x100, 0x43f8); // LEA $1100.W,A1.
    immediateTarget.putWord(0x102, 0x1100);
    immediateTarget.putWord(0x104, 0x33bc); // MOVE.W #$1234,(0,A1,D0.W).
    immediateTarget.putWord(0x106, 0x1234);
    immediateTarget.putWord(0x108, 0x0000);
    Moto68000 immediateTargetCpu(immediateTarget);
    immediateTargetCpu.reset();
    run(immediateTargetCpu, 40 + 8 + 18);
    check(immediateTargetCpu.state().pc == 0x10a &&
              immediateTarget.byte(0x1100) == 0x12 && immediateTarget.byte(0x1101) == 0x34 &&
              (immediateTargetCpu.state().sr & 0x0f) == 0,
          "MOVE.W imediato para destino indexado consome ambas as extensões");

    Bus memoryTarget;
    memoryTarget.setup();
    memoryTarget.putWord(0x100, 0x41f8); // LEA $1200.W,A0.
    memoryTarget.putWord(0x102, 0x1200);
    memoryTarget.putWord(0x104, 0x43f8); // LEA $1300.W,A1.
    memoryTarget.putWord(0x106, 0x1300);
    memoryTarget.putWord(0x108, 0x7004); // MOVEQ #4,D0.
    memoryTarget.putWord(0x10a, 0x3390); // MOVE.W (A0),(-4,A1,D0.W).
    memoryTarget.putWord(0x10c, 0x00fc);
    memoryTarget.putWord(0x1200, 0x8001);
    Moto68000 memoryTargetCpu(memoryTarget);
    memoryTargetCpu.reset();
    run(memoryTargetCpu, 40 + 8 + 8 + 4 + 18);
    check(memoryTargetCpu.state().pc == 0x10e &&
              memoryTarget.byte(0x1300) == 0x80 && memoryTarget.byte(0x1301) == 1 &&
              (memoryTargetCpu.state().sr & 0x0f) == 8,
          "MOVE.W entre memórias usa o destino indexado em 18 ciclos");

    Bus addressIndex;
    addressIndex.setup();
    addressIndex.putWord(0x100, 0x41f8);
    addressIndex.putWord(0x102, 0x1104);
    addressIndex.putWord(0x104, 0x7004); // MOVEQ #4,D0
    addressIndex.putWord(0x106, 0x1230); // MOVE.B (-8,A0,D0.W),D1
    addressIndex.putWord(0x108, 0x00f8);
    addressIndex.bytes[0x1100] = 0x80;
    Moto68000 addressIndexCpu(addressIndex);
    addressIndexCpu.reset();
    run(addressIndexCpu, 40 + 8 + 4 + 14);
    check(addressIndexCpu.state().pc == 0x10a &&
              addressIndexCpu.state().data[1] == 0x80 &&
              addressIndexCpu.state().address[0] == 0x1104 &&
              (addressIndexCpu.state().sr & 0x0f) == 8 &&
              addressIndex.saw(0x1100, 5),
          "indexação por An combina deslocamento negativo e índice Dn.W");

    Bus addressRegisterIndex;
    addressRegisterIndex.setup();
    addressRegisterIndex.putWord(0x100, 0x41f8);
    addressRegisterIndex.putWord(0x102, 0x1104);
    addressRegisterIndex.putWord(0x104, 0x43f8);
    addressRegisterIndex.putWord(0x106, 0xfffc);
    addressRegisterIndex.putWord(0x108, 0x2430); // MOVE.L (0,A0,A1.L),D2
    addressRegisterIndex.putWord(0x10a, 0x9e00); // Bits de escala são ignorados no 68000.
    addressRegisterIndex.putWord(0x1100, 0x1234);
    addressRegisterIndex.putWord(0x1102, 0x5678);
    Moto68000 addressRegisterIndexCpu(addressRegisterIndex);
    addressRegisterIndexCpu.reset();
    run(addressRegisterIndexCpu, 40 + 8 + 8 + 18);
    check(addressRegisterIndexCpu.state().data[2] == 0x12345678 &&
              addressRegisterIndexCpu.state().address[1] == 0xfffffffc &&
              addressRegisterIndex.saw(0x1100, 5) &&
              addressRegisterIndex.saw(0x1102, 5),
          "índice An.L usa sinal de 32 bits e ignora escala");

    Bus indexSize;
    indexSize.setup();
    indexSize.putWord(0x100, 0x203c); // MOVE.L #$00010004,D0
    indexSize.putWord(0x102, 1);
    indexSize.putWord(0x104, 4);
    indexSize.putWord(0x106, 0x41f8);
    indexSize.putWord(0x108, 0x1100);
    indexSize.putWord(0x10a, 0x3230); // MOVE.W (0,A0,D0.W),D1
    indexSize.putWord(0x10c, 0x0000);
    indexSize.putWord(0x10e, 0x3430); // MOVE.W (0,A0,D0.L),D2
    indexSize.putWord(0x110, 0x0800);
    indexSize.putWord(0x1104, 0x1111);
    indexSize.putWord(0x11104, 0x2222);
    Moto68000 indexSizeCpu(indexSize);
    indexSizeCpu.reset();
    run(indexSizeCpu, 40 + 12 + 8 + 14 + 14);
    check(indexSizeCpu.state().pc == 0x112 &&
              indexSizeCpu.state().data[1] == 0x1111 &&
              indexSizeCpu.state().data[2] == 0x2222 &&
              indexSize.saw(0x1104, 5) && indexSize.saw(0x11104, 5),
          "índices Dn.W e Dn.L produzem endereços diferentes");

    Bus negativeWordIndex;
    negativeWordIndex.setup();
    negativeWordIndex.putWord(0x100, 0x203c); // MOVE.L #$0000FFFC,D0
    negativeWordIndex.putWord(0x102, 0);
    negativeWordIndex.putWord(0x104, 0xfffc);
    negativeWordIndex.putWord(0x106, 0x41f8);
    negativeWordIndex.putWord(0x108, 0x1104);
    negativeWordIndex.putWord(0x10a, 0x3230); // MOVE.W (0,A0,D0.W),D1
    negativeWordIndex.putWord(0x10c, 0);
    negativeWordIndex.putWord(0x1100, 0x8001);
    Moto68000 negativeWordIndexCpu(negativeWordIndex);
    negativeWordIndexCpu.reset();
    run(negativeWordIndexCpu, 40 + 12 + 8 + 14);
    check(negativeWordIndexCpu.state().data[1] == 0x8001 &&
              negativeWordIndex.saw(0x1100, 5),
          "índice de palavra negativo recebe extensão de sinal");

    Bus pcDisplacement;
    pcDisplacement.setup();
    pcDisplacement.putWord(0x100, 0x323a); // MOVE.W (-4,PC),D1
    pcDisplacement.putWord(0x102, 0xfffc);
    pcDisplacement.putWord(0x0fe, 0x8001);
    Moto68000 pcDisplacementCpu(pcDisplacement);
    pcDisplacementCpu.reset();
    run(pcDisplacementCpu, 40 + 12);
    check(pcDisplacementCpu.state().pc == 0x104 &&
              pcDisplacementCpu.state().data[1] == 0x8001 &&
              pcDisplacement.saw(0x0fe, 6),
          "deslocamento relativo usa o endereço da extensão e espaço de programa");

    Bus pcIndex;
    pcIndex.setup();
    pcIndex.putWord(0x100, 0x7004);
    pcIndex.putWord(0x102, 0x323b); // MOVE.W (40,PC,D0.W),D1
    pcIndex.putWord(0x104, 0x0028);
    pcIndex.putWord(0x130, 0x1234);
    Moto68000 pcIndexCpu(pcIndex);
    pcIndexCpu.reset();
    run(pcIndexCpu, 40 + 4 + 14);
    check(pcIndexCpu.state().pc == 0x106 &&
              pcIndexCpu.state().data[1] == 0x1234 &&
              pcIndex.saw(0x130, 6),
          "indexação relativa ao PC lê o operando no espaço de programa");

    Bus pcMovea;
    pcMovea.setup();
    pcMovea.putWord(0x100, 0x7400); // MOVEQ #0,D2: Z=1
    pcMovea.putWord(0x102, 0x327a); // MOVEA.W (44,PC),A1
    pcMovea.putWord(0x104, 0x002c);
    pcMovea.putWord(0x130, 0x8001);
    Moto68000 pcMoveaCpu(pcMovea);
    pcMoveaCpu.reset();
    run(pcMoveaCpu, 40 + 4 + 12);
    check(pcMoveaCpu.state().address[1] == 0xffff8001 &&
              (pcMoveaCpu.state().sr & 0x1f) == 4 &&
              pcMovea.saw(0x130, 6),
          "MOVEA.W relativo ao PC estende sinal e preserva CCR");

    Bus pcIndexedMovea;
    pcIndexedMovea.setup();
    pcIndexedMovea.putWord(0x100, 0x41f8);
    pcIndexedMovea.putWord(0x102, 4);
    pcIndexedMovea.putWord(0x104, 0x227b); // MOVEA.L (38,PC,A0.W),A1
    pcIndexedMovea.putWord(0x106, 0x8026);
    pcIndexedMovea.putWord(0x130, 0x1234);
    pcIndexedMovea.putWord(0x132, 0x5678);
    Moto68000 pcIndexedMoveaCpu(pcIndexedMovea);
    pcIndexedMoveaCpu.reset();
    run(pcIndexedMoveaCpu, 40 + 8 + 18);
    check(pcIndexedMoveaCpu.state().address[1] == 0x12345678 &&
              pcIndexedMovea.saw(0x130, 6) &&
              pcIndexedMovea.saw(0x132, 6),
          "MOVEA.L relativo ao PC aceita índice de endereço");

    Bus indexedToAbsolute;
    indexedToAbsolute.setup();
    indexedToAbsolute.putWord(0x100, 0x41f8);
    indexedToAbsolute.putWord(0x102, 0x1100);
    indexedToAbsolute.putWord(0x104, 0x7004);
    indexedToAbsolute.putWord(0x106, 0x23f0); // MOVE.L (0,A0,D0.W),($00001300).L
    indexedToAbsolute.putWord(0x108, 0);
    indexedToAbsolute.putWord(0x10a, 0);
    indexedToAbsolute.putWord(0x10c, 0x1300);
    indexedToAbsolute.putWord(0x1104, 0x1234);
    indexedToAbsolute.putWord(0x1106, 0x5678);
    Moto68000 indexedToAbsoluteCpu(indexedToAbsolute);
    indexedToAbsoluteCpu.reset();
    run(indexedToAbsoluteCpu, 40 + 8 + 4 + 34);
    check(indexedToAbsoluteCpu.state().pc == 0x10e &&
              indexedToAbsolute.byte(0x1300) == 0x12 &&
              indexedToAbsolute.byte(0x1301) == 0x34 &&
              indexedToAbsolute.byte(0x1302) == 0x56 &&
              indexedToAbsolute.byte(0x1303) == 0x78,
          "MOVE.L indexado transfere para destino absoluto longo");

    Bus pcToMemory;
    pcToMemory.setup();
    pcToMemory.putWord(0x100, 0x41f8);
    pcToMemory.putWord(0x102, 0x1200);
    pcToMemory.putWord(0x104, 0x30ba); // MOVE.W (42,PC),(A0)
    pcToMemory.putWord(0x106, 0x002a);
    pcToMemory.putWord(0x130, 0x8001);
    Moto68000 pcToMemoryCpu(pcToMemory);
    pcToMemoryCpu.reset();
    run(pcToMemoryCpu, 40 + 8 + 16);
    check(pcToMemoryCpu.state().pc == 0x108 &&
              pcToMemory.byte(0x1200) == 0x80 &&
              pcToMemory.byte(0x1201) == 0x01 &&
              pcToMemory.saw(0x130, 6),
          "MOVE.W relativo ao PC pode escrever em memória");

    Bus waitedPc;
    waitedPc.setup();
    waitedPc.putWord(0x100, 0x323a); // MOVE.W (46,PC),D1
    waitedPc.putWord(0x102, 0x002e);
    waitedPc.putWord(0x130, 0x5678);
    waitedPc.waitAddress = 0x130;
    waitedPc.readWait = 2;
    Moto68000 waitedPcCpu(waitedPc);
    waitedPcCpu.reset();
    run(waitedPcCpu, 40 + 8);
    check(waitedPcCpu.state().pc == 0x100 &&
              waitedPcCpu.pins().waiting &&
              waitedPcCpu.pins().functionCode == 6,
          "espera de leitura relativa ao PC mantém espaço de programa");
    run(waitedPcCpu, 2 + 4);
    check(waitedPcCpu.state().pc == 0x104 &&
              waitedPcCpu.state().data[1] == 0x5678,
          "MOVE relativo ao PC conclui após a espera");
}
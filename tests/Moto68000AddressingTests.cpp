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
        std::vector<std::uint32_t> reads, writes;
        std::vector<M68KPins> trace;
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
            writes.push_back(address);
            bytes[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            writes.push_back(address);
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
        void observe(const M68KPins &pins) override { trace.push_back(pins); }
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

    void setup(Bus &bus, std::uint16_t stack = 0x1800)
    {
        bus.putWord(0, 0);
        bus.putWord(2, stack);
        bus.putWord(4, 0);
        bus.putWord(6, 0x0100);
        bus.putWord(12, 0);
        bus.putWord(14, 0x0200);
        for (unsigned address = 0x100; address < 0x120; address += 2)
            bus.putWord(address, 0x4e71);
        bus.putWord(0x200, 0x4e71);
        bus.putWord(0x202, 0x4e71);
    }
}

int main()
{
    Bus postByte;
    setup(postByte);
    postByte.putWord(0x100, 0x41f8);
    postByte.putWord(0x102, 0x1100); // LEA $1100.W,A0
    postByte.putWord(0x104, 0x1218); // MOVE.B (A0)+,D1
    postByte.bytes[0x1100] = 0x80;
    Moto68000 postByteCpu(postByte);
    postByteCpu.reset();
    run(postByteCpu, 40 + 8 + 8);
    check(postByteCpu.state().data[1] == 0x80 && postByteCpu.state().address[0] == 0x1101 &&
              (postByteCpu.state().sr & 0x0f) == 8,
          "MOVE.B com pós-incremento");

    Bus preWord;
    setup(preWord);
    preWord.putWord(0x100, 0x41f8);
    preWord.putWord(0x102, 0x1102);
    preWord.putWord(0x104, 0x3220); // MOVE.W -(A0),D1
    preWord.putWord(0x1100, 0x8001);
    Moto68000 preWordCpu(preWord);
    preWordCpu.reset();
    run(preWordCpu, 40 + 8 + 10);
    check(preWordCpu.state().data[1] == 0x8001 && preWordCpu.state().address[0] == 0x1100 &&
              (preWordCpu.state().sr & 0x0f) == 8,
          "MOVE.W com pré-decremento");

    Bus preLong;
    setup(preLong);
    preLong.putWord(0x100, 0x41f8);
    preLong.putWord(0x102, 0x1104);
    preLong.putWord(0x104, 0x2220); // MOVE.L -(A0),D1
    preLong.putWord(0x1100, 0x1234);
    preLong.putWord(0x1102, 0x5678);
    Moto68000 preLongCpu(preLong);
    preLongCpu.reset();
    run(preLongCpu, 40 + 8 + 14);
    check(preLongCpu.state().data[1] == 0x12345678 &&
              preLongCpu.state().address[0] == 0x1100,
          "MOVE.L lê duas palavras após pré-decremento");

    Bus preStore;
    setup(preStore);
    preStore.putWord(0x100, 0x41f8);
    preStore.putWord(0x102, 0x1200);
    preStore.putWord(0x104, 0x2010); // MOVE.L (A0),D0
    preStore.putWord(0x106, 0x41f8);
    preStore.putWord(0x108, 0x1104);
    preStore.putWord(0x10a, 0x2100); // MOVE.L D0,-(A0)
    preStore.putWord(0x1200, 0x1234);
    preStore.putWord(0x1202, 0x5678);
    Moto68000 preStoreCpu(preStore);
    preStoreCpu.reset();
    run(preStoreCpu, 40 + 8 + 12 + 8 + 12);
    check(preStoreCpu.state().address[0] == 0x1100 &&
              preStore.byte(0x1100) == 0x12 && preStore.byte(0x1101) == 0x34 &&
              preStore.byte(0x1102) == 0x56 && preStore.byte(0x1103) == 0x78,
          "MOVE.L grava o valor completo em pré-decremento");
    check(preStore.writes == std::vector<std::uint32_t>({0x1102, 0x1100}),
          "pré-decremento longo grava palavra baixa antes da alta");

    Bus postStore;
    setup(postStore);
    postStore.putWord(0x100, 0x41f8);
    postStore.putWord(0x102, 0x1200);
    postStore.putWord(0x104, 0x2010);
    postStore.putWord(0x106, 0x41f8);
    postStore.putWord(0x108, 0x1100);
    postStore.putWord(0x10a, 0x20c0); // MOVE.L D0,(A0)+
    postStore.putWord(0x1200, 0x1234);
    postStore.putWord(0x1202, 0x5678);
    Moto68000 postStoreCpu(postStore);
    postStoreCpu.reset();
    run(postStoreCpu, 40 + 8 + 12 + 8 + 12);
    check(postStoreCpu.state().address[0] == 0x1104 &&
              postStore.writes == std::vector<std::uint32_t>({0x1100, 0x1102}) &&
              postStore.byte(0x1100) == 0x12 && postStore.byte(0x1102) == 0x56,
          "MOVE.L com pós-incremento grava em ordem crescente");

    Bus stackPost;
    setup(stackPost);
    stackPost.putWord(0x100, 0x121f); // MOVE.B (A7)+,D1
    stackPost.bytes[0x1800] = 0x7e;
    Moto68000 stackPostCpu(stackPost);
    stackPostCpu.reset();
    run(stackPostCpu, 40 + 8);
    check(stackPostCpu.state().data[1] == 0x7e && stackPostCpu.state().address[7] == 0x1802,
          "A7 avança dois bytes após leitura de byte");

    Bus stackPre;
    setup(stackPre, 0x1802);
    stackPre.putWord(0x100, 0x707f); // MOVEQ #127,D0
    stackPre.putWord(0x102, 0x1f00); // MOVE.B D0,-(A7)
    Moto68000 stackPreCpu(stackPre);
    stackPreCpu.reset();
    run(stackPreCpu, 40 + 4 + 8);
    check(stackPreCpu.state().address[7] == 0x1800 && stackPre.byte(0x1800) == 0x7f &&
              stackPre.writes == std::vector<std::uint32_t>{0x1800},
          "A7 recua dois bytes antes da escrita de byte");

    Bus waitedStore;
    setup(waitedStore, 0x1802);
    waitedStore.putWord(0x100, 0x707f);
    waitedStore.putWord(0x102, 0x1f00); // MOVE.B D0,-(A7)
    waitedStore.waitAddress = 0x1800;
    waitedStore.writeWait = 2;
    Moto68000 waitedStoreCpu(waitedStore);
    waitedStoreCpu.reset();
    run(waitedStoreCpu, 40 + 4 + 8);
    check(waitedStoreCpu.state().address[7] == 0x1800 &&
              waitedStoreCpu.pins().waiting && waitedStore.writes.empty(),
          "pré-decremento não se repete durante espera de escrita");
    run(waitedStoreCpu, 2);
    check(waitedStoreCpu.state().address[7] == 0x1800 &&
              waitedStore.writes == std::vector<std::uint32_t>{0x1800} &&
              waitedStore.byte(0x1800) == 0x7f,
          "escrita confirmada uma vez após a espera");

    Bus waited;
    setup(waited);
    waited.putWord(0x100, 0x41f8);
    waited.putWord(0x102, 0x1100);
    waited.putWord(0x104, 0x1218);
    waited.bytes[0x1100] = 0x55;
    waited.waitAddress = 0x1100;
    waited.readWait = 2;
    Moto68000 waitedCpu(waited);
    waitedCpu.reset();
    run(waitedCpu, 40 + 8 + 4);
    check(waitedCpu.state().address[0] == 0x1100 && waitedCpu.pins().waiting,
          "A0 não avança durante a espera");
    run(waitedCpu, 2 + 4);
    check(waitedCpu.state().address[0] == 0x1101 && waitedCpu.state().data[1] == 0x55,
          "A0 avança após leitura confirmada");

    Bus odd;
    setup(odd);
    odd.putWord(0x100, 0x41f8);
    odd.putWord(0x102, 0x1101);
    odd.putWord(0x104, 0x3220); // MOVE.W -(A0),D1: endereço $10FF.
    Moto68000 oddCpu(odd);
    oddCpu.reset();
    run(oddCpu, 40 + 8 + 2 + 50);
    check(oddCpu.state().pc == 0x200 && oddCpu.state().address[0] == 0x10ff &&
              !oddCpu.halted(),
          "pré-decremento precede erro de endereço");

    Bus displacedRead;
    setup(displacedRead);
    displacedRead.putWord(0x100, 0x41f8);
    displacedRead.putWord(0x102, 0x1104);
    displacedRead.putWord(0x104, 0x3228); // MOVE.W (-4,A0),D1
    displacedRead.putWord(0x106, 0xfffc);
    displacedRead.putWord(0x1100, 0x8001);
    Moto68000 displacedReadCpu(displacedRead);
    displacedReadCpu.reset();
    run(displacedReadCpu, 40 + 8 + 12);
    check(displacedReadCpu.state().pc == 0x108 &&
              displacedReadCpu.state().address[0] == 0x1104 &&
              displacedReadCpu.state().data[1] == 0x8001 &&
              displacedReadCpu.state().prefetch[0] == 0x4e71,
          "MOVE.W com deslocamento negativo e pré-busca");

    Bus absoluteShortRead;
    setup(absoluteShortRead);
    absoluteShortRead.putWord(0x100, 0x1238); // MOVE.B ($FF01).W,D1
    absoluteShortRead.putWord(0x102, 0xff01);
    absoluteShortRead.bytes[0xffff01] = 0xa5;
    Moto68000 absoluteShortReadCpu(absoluteShortRead);
    absoluteShortReadCpu.reset();
    run(absoluteShortReadCpu, 40 + 12);
    check(absoluteShortReadCpu.state().data[1] == 0xa5 &&
              (absoluteShortReadCpu.state().sr & 0x0f) == 8 &&
              absoluteShortRead.reads[absoluteShortRead.reads.size() - 2] == 0xffff01,
          "endereço absoluto curto estendido com sinal");

    Bus absoluteLongRead;
    setup(absoluteLongRead);
    absoluteLongRead.putWord(0x100, 0x2439); // MOVE.L ($00001200).L,D2
    absoluteLongRead.putWord(0x102, 0x0000);
    absoluteLongRead.putWord(0x104, 0x1200);
    absoluteLongRead.putWord(0x1200, 0x1234);
    absoluteLongRead.putWord(0x1202, 0x5678);
    Moto68000 absoluteLongReadCpu(absoluteLongRead);
    absoluteLongReadCpu.reset();
    run(absoluteLongReadCpu, 40 + 20);
    check(absoluteLongReadCpu.state().pc == 0x106 &&
              absoluteLongReadCpu.state().data[2] == 0x12345678 &&
              absoluteLongReadCpu.state().prefetch[0] == 0x4e71,
          "MOVE.L lê endereço absoluto longo e suas duas palavras de dados");

    Bus immediate;
    setup(immediate);
    immediate.putWord(0x100, 0x223c); // MOVE.L #$11223344,D1
    immediate.putWord(0x102, 0x1122);
    immediate.putWord(0x104, 0x3344);
    immediate.putWord(0x106, 0x123c); // MOVE.B #$FF,D1
    immediate.putWord(0x108, 0x00ff);
    immediate.putWord(0x10a, 0x323c); // MOVE.W #$8001,D1
    immediate.putWord(0x10c, 0x8001);
    Moto68000 immediateCpu(immediate);
    immediateCpu.reset();
    run(immediateCpu, 40 + 12 + 8);
    check(immediateCpu.state().pc == 0x10a &&
              immediateCpu.state().data[1] == 0x112233ff &&
              (immediateCpu.state().sr & 0x0f) == 8,
          "MOVE imediato longo e byte preserva os bits altos de D1");
    run(immediateCpu, 8);
    check(immediateCpu.state().pc == 0x10e &&
              immediateCpu.state().data[1] == 0x11228001 &&
              (immediateCpu.state().sr & 0x0f) == 8,
          "MOVE imediato de palavra preserva a metade alta de D1");

    Bus displacedWrite;
    setup(displacedWrite);
    displacedWrite.putWord(0x100, 0x7034); // MOVEQ #$34,D0
    displacedWrite.putWord(0x102, 0x41f8);
    displacedWrite.putWord(0x104, 0x1104);
    displacedWrite.putWord(0x106, 0x3140); // MOVE.W D0,(-4,A0)
    displacedWrite.putWord(0x108, 0xfffc);
    Moto68000 displacedWriteCpu(displacedWrite);
    displacedWriteCpu.reset();
    run(displacedWriteCpu, 40 + 4 + 8 + 12);
    check(displacedWriteCpu.state().pc == 0x10a &&
              displacedWriteCpu.state().address[0] == 0x1104 &&
              displacedWrite.byte(0x1100) == 0 && displacedWrite.byte(0x1101) == 0x34 &&
              displacedWrite.writes == std::vector<std::uint32_t>{0x1100},
          "MOVE.W escreve em endereço com deslocamento");

    Bus absoluteShortWrite;
    setup(absoluteShortWrite);
    absoluteShortWrite.putWord(0x100, 0x707e);
    absoluteShortWrite.putWord(0x102, 0x11c0); // MOVE.B D0,($FF00).W
    absoluteShortWrite.putWord(0x104, 0xff00);
    Moto68000 absoluteShortWriteCpu(absoluteShortWrite);
    absoluteShortWriteCpu.reset();
    run(absoluteShortWriteCpu, 40 + 4 + 12);
    check(absoluteShortWrite.byte(0xffff00) == 0x7e &&
              absoluteShortWrite.writes == std::vector<std::uint32_t>{0xffff00},
          "MOVE.B escreve em endereço absoluto curto");

    Bus absoluteLongWrite;
    setup(absoluteLongWrite);
    absoluteLongWrite.putWord(0x100, 0x203c); // MOVE.L #$12345678,D0
    absoluteLongWrite.putWord(0x102, 0x1234);
    absoluteLongWrite.putWord(0x104, 0x5678);
    absoluteLongWrite.putWord(0x106, 0x23c0); // MOVE.L D0,($00001100).L
    absoluteLongWrite.putWord(0x108, 0);
    absoluteLongWrite.putWord(0x10a, 0x1100);
    Moto68000 absoluteLongWriteCpu(absoluteLongWrite);
    absoluteLongWriteCpu.reset();
    run(absoluteLongWriteCpu, 40 + 12 + 20);
    check(absoluteLongWriteCpu.state().pc == 0x10c &&
              absoluteLongWrite.writes == std::vector<std::uint32_t>({0x1100, 0x1102}) &&
              absoluteLongWrite.byte(0x1100) == 0x12 &&
              absoluteLongWrite.byte(0x1101) == 0x34 &&
              absoluteLongWrite.byte(0x1102) == 0x56 &&
              absoluteLongWrite.byte(0x1103) == 0x78,
          "MOVE.L escreve em endereço absoluto longo");

    Bus displacedWait;
    setup(displacedWait);
    displacedWait.putWord(0x100, 0x41f8);
    displacedWait.putWord(0x102, 0x1104);
    displacedWait.putWord(0x104, 0x3228);
    displacedWait.putWord(0x106, 0xfffc);
    displacedWait.putWord(0x1100, 0x5678);
    displacedWait.waitAddress = 0x1100;
    displacedWait.readWait = 2;
    Moto68000 displacedWaitCpu(displacedWait);
    displacedWaitCpu.reset();
    run(displacedWaitCpu, 40 + 8 + 8);
    check(displacedWaitCpu.state().pc == 0x104 && displacedWaitCpu.pins().waiting &&
              displacedWaitCpu.state().address[0] == 0x1104,
          "espera na leitura com deslocamento mantém PC e A0");
    run(displacedWaitCpu, 2 + 4);
    check(displacedWaitCpu.state().pc == 0x108 &&
              displacedWaitCpu.state().data[1] == 0x5678,
          "MOVE com deslocamento conclui após a espera");

    Bus absoluteWriteWait;
    setup(absoluteWriteWait);
    absoluteWriteWait.putWord(0x100, 0x707e);
    absoluteWriteWait.putWord(0x102, 0x31c0); // MOVE.W D0,($1100).W
    absoluteWriteWait.putWord(0x104, 0x1100);
    absoluteWriteWait.waitAddress = 0x1100;
    absoluteWriteWait.writeWait = 2;
    Moto68000 absoluteWriteWaitCpu(absoluteWriteWait);
    absoluteWriteWaitCpu.reset();
    run(absoluteWriteWaitCpu, 40 + 4 + 8);
    check(absoluteWriteWaitCpu.state().pc == 0x102 &&
              absoluteWriteWaitCpu.pins().waiting && absoluteWriteWait.writes.empty(),
          "espera na escrita absoluta mantém a instrução pendente");
    run(absoluteWriteWaitCpu, 2 + 4);
    check(absoluteWriteWaitCpu.state().pc == 0x106 &&
              absoluteWriteWait.writes == std::vector<std::uint32_t>{0x1100} &&
              absoluteWriteWait.byte(0x1101) == 0x7e,
          "escrita absoluta ocorre uma vez após a espera");
}
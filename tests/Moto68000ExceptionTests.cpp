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
        std::unordered_map<std::uint32_t, std::uint16_t> words;
        std::vector<std::uint32_t> reads, writes;
        std::vector<M68KPins> trace;
        std::uint32_t failingAddress = 0xffffffff;
        bool failingWrite = false;
        unsigned waits = 0;

        std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte inesperado"); }
        std::uint16_t readWord(std::uint32_t address) override
        {
            if (!failingWrite && address == failingAddress)
                throw M68KBusError("falha de leitura");
            reads.push_back(address);
            return words.count(address) ? words.at(address) : 0x4e71;
        }
        bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
        {
            if (address == failingAddress && waits) { --waits; return false; }
            value = readWord(address);
            return true;
        }
        void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("byte inesperado"); }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            if (failingWrite && address == failingAddress)
                throw M68KBusError("falha de escrita");
            writes.push_back(address);
            words[address] = value;
        }
        void observe(const M68KPins &pins) override { trace.push_back(pins); }
    };

    void check(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void run(Moto68000 &cpu, unsigned cycles)
    {
        while (cycles--) cpu.clock();
    }

    void setup(Bus &bus, std::uint16_t operandAddress, std::uint16_t opcode)
    {
        bus.words[0] = 0; bus.words[2] = 0x1000; // SSP
        bus.words[4] = 0; bus.words[6] = 0x0100; // PC
        bus.words[8] = 0; bus.words[10] = 0x0300; // Erro de barramento
        bus.words[12] = 0; bus.words[14] = 0x0200; // Erro de endereço
        bus.words[0x100] = 0x41f8; bus.words[0x102] = operandAddress;
        bus.words[0x104] = opcode; bus.words[0x106] = 0x4e71;
        bus.words[0x108] = 0x4e71;
        bus.words[0x200] = bus.words[0x300] = 0x7412; // MOVEQ #$12,D2
        bus.words[0x202] = bus.words[0x302] = 0x4e71;
        bus.words[0x204] = bus.words[0x304] = 0x4e71;
    }

    void checkFrame(const Bus &bus, std::uint16_t opcode,
                    std::uint16_t address, std::uint16_t status)
    {
        check(bus.words.at(0xff2) == 0x2700, "SR salvo");
        check(bus.words.at(0xff4) == 0 && bus.words.at(0xff6) == 0x104, "PC salvo");
        check(bus.words.at(0xff8) == opcode, "IR salvo");
        check(bus.words.at(0xffa) == 0 && bus.words.at(0xffc) == address, "endereço salvo");
        check(bus.words.at(0xffe) == status, "estado da transferência salvo");
        check(bus.writes.size() == 7, "sete palavras de exceção");
    }
}

int main()
{
    Bus odd;
    setup(odd, 0x0101, 0x3010); // MOVE.W (A0),D0 em endereço ímpar
    Moto68000 oddCpu(odd);
    oddCpu.reset();
    run(oddCpu, 48);
    check(oddCpu.state().pc == 0x104, "LEA concluído");
    run(oddCpu, 50);
    check(!oddCpu.halted() && oddCpu.state().pc == 0x200, "vetor de erro de endereço");
    check(oddCpu.state().address[7] == 0xff2, "pilha da exceção");
    checkFrame(odd, 0x3010, 0x0101, 0x15);
    for (const auto &pins : odd.trace)
        check(!(pins.active && pins.address == 0x0101), "acesso ímpar não chegou ao barramento");
    run(oddCpu, 4);
    check(oddCpu.state().data[2] == 0x12 && oddCpu.state().pc == 0x202,
          "execução do tratador");

    Bus oddWrite;
    setup(oddWrite, 0x0101, 0x3080); // MOVE.W D0,(A0) em endereço ímpar
    Moto68000 oddWriteCpu(oddWrite);
    oddWriteCpu.reset();
    run(oddWriteCpu, 48 + 4 + 50);
    check(oddWriteCpu.state().pc == 0x200, "vetor de escrita desalinhada");
    checkFrame(oddWrite, 0x3080, 0x0101, 0x05);
    for (const auto &pins : oddWrite.trace)
        check(!(pins.active && pins.address == 0x0101), "escrita ímpar não chegou ao barramento");

    Bus oddInstruction;
    setup(oddInstruction, 0x0400, 0x3010);
    oddInstruction.words[0x100] = 0x6001; // BRA para $103, endereço de instrução ímpar
    oddInstruction.words[0x102] = 0x4e71;
    Moto68000 instructionCpu(oddInstruction);
    instructionCpu.reset();
    run(instructionCpu, 40 + 2 + 50);
    check(instructionCpu.state().pc == 0x200 && !instructionCpu.halted(),
          "erro de endereço na busca de instrução");
    check(oddInstruction.words.at(0xff8) == 0x6001 &&
          oddInstruction.words.at(0xffc) == 0x0103 &&
          oddInstruction.words.at(0xffe) == 0x16, "quadro da busca de instrução");

    Bus readFault;
    setup(readFault, 0x0400, 0x3010);
    readFault.failingAddress = 0x0400;
    readFault.waits = 2;
    Moto68000 readCpu(readFault);
    readCpu.reset();
    run(readCpu, 48 + 2 + 3 + 49);
    check(!readCpu.halted() && readCpu.state().pc == 0x300, "vetor de erro de leitura");
    checkFrame(readFault, 0x3010, 0x0400, 0x15);

    Bus writeFault;
    setup(writeFault, 0x0400, 0x3080); // MOVE.W D0,(A0)
    writeFault.failingAddress = 0x0400;
    writeFault.failingWrite = true;
    Moto68000 writeCpu(writeFault);
    writeCpu.reset();
    run(writeCpu, 48 + 4 + 3 + 49);
    check(!writeCpu.halted() && writeCpu.state().pc == 0x300, "vetor de erro de escrita");
    checkFrame(writeFault, 0x3080, 0x0400, 0x05);

    Bus doubleFault;
    setup(doubleFault, 0x0101, 0x3010);
    doubleFault.failingAddress = 0xffe;
    doubleFault.failingWrite = true;
    Moto68000 doubleCpu(doubleFault);
    doubleCpu.reset();
    run(doubleCpu, 48 + 20);
    check(doubleCpu.halted() && doubleCpu.state().pc == 0x104,
          "falha durante a exceção interrompe a CPU");

    Bus badHandler;
    setup(badHandler, 0x0101, 0x3010);
    badHandler.words[14] = 0x0201; // Vetor de erro de endereço aponta para endereço ímpar.
    Moto68000 badHandlerCpu(badHandler);
    badHandlerCpu.reset();
    run(badHandlerCpu, 48 + 50);
    check(badHandlerCpu.halted(), "falha na busca do tratador interrompe a CPU");

    Bus badReset;
    setup(badReset, 0x0400, 0x3010);
    badReset.failingAddress = 0;
    Moto68000 badResetCpu(badReset);
    badResetCpu.reset();
    run(badResetCpu, 16 + 3);
    check(badResetCpu.halted() && !badResetCpu.resetComplete(),
          "falha na leitura do vetor de reset interrompe a CPU");

    Bus userFault;
    setup(userFault, 0x0101, 0x3010);
    userFault.words[0x100] = 0x4e73; // RTE carrega o SR de usuário.
    userFault.words[0x1000] = 0; userFault.words[0x1002] = 0;
    userFault.words[0x1004] = 0x0120;
    userFault.words[0x120] = 0x41f8; userFault.words[0x122] = 0x0101;
    userFault.words[0x124] = 0x3010; userFault.words[0x126] = 0x4e71;
    userFault.words[0x128] = 0x4e71;
    Moto68000 userFaultCpu(userFault);
    userFaultCpu.reset();
    run(userFaultCpu, 40 + 20 + 8 + 50);
    check(userFaultCpu.state().pc == 0x200 && userFaultCpu.state().sr == 0x2000 &&
          userFaultCpu.state().address[7] == 0xff8,
          "erro de usuário troca para o SSP");
    check(userFault.words.at(0xff8) == 0 && userFault.words.at(0xffc) == 0x124 &&
          userFault.words.at(0xffe) == 0x3010 && userFault.words.at(0x1004) == 0x11,
          "quadro de erro conserva modo usuário e código de função 1");

    Bus exceptionFault;
    setup(exceptionFault, 0x0400, 0x3010);
    exceptionFault.words[0x100] = 0x4afc; // ILLEGAL usa o vetor 4.
    exceptionFault.failingAddress = 0x10; // Falha na leitura do vetor 4.
    Moto68000 exceptionFaultCpu(exceptionFault);
    exceptionFaultCpu.reset();
    run(exceptionFaultCpu, 40 + 21 + 49);
    check(!exceptionFaultCpu.halted() && exceptionFaultCpu.state().pc == 0x300 &&
          exceptionFaultCpu.state().address[7] == 0xfec,
          "erro de barramento durante exceção comum usa o vetor 2");
    check(exceptionFault.words.at(0xff0) == 0x0010 &&
          exceptionFault.words.at(0xff6) == 0x0010 &&
          exceptionFault.words.at(0xff8) == 0x1d,
          "quadro registra vetor e contexto de exceção");
}

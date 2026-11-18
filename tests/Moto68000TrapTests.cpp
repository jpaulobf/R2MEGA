#include "Core/CPU/Moto68000/Moto68000.h"
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    struct Bus : M68KBus
    {
        std::unordered_map<std::uint32_t, std::uint16_t> words;
        std::vector<std::uint32_t> reads, writes;
        std::vector<M68KPins> trace;
        std::uint32_t waitAddress = 0xffffffff;
        std::uint32_t failingRead = 0xffffffff;
        unsigned readWait = 0, writeWait = 0;
        std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte inesperado"); }
        std::uint16_t readWord(std::uint32_t address) override
        {
            if (address == failingRead) throw M68KBusError("falha de leitura");
            reads.push_back(address);
            return words.count(address) ? words.at(address) : 0x4e71;
        }
        bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
        {
            if (address == waitAddress && readWait) { --readWait; return false; }
            value = readWord(address);
            return true;
        }
        void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("byte inesperado"); }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            writes.push_back(address);
            words[address] = value;
        }
        bool tryWriteWord(std::uint32_t address, std::uint16_t value) override
        {
            if (address == waitAddress && writeWait) { --writeWait; return false; }
            writeWord(address, value);
            return true;
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

    void setup(Bus &bus, std::uint16_t opcode, unsigned vector, std::uint16_t handler)
    {
        bus.words[0] = 0; bus.words[2] = 0x1000;
        bus.words[4] = 0; bus.words[6] = 0x0100;
        bus.words[vector * 4] = 0; bus.words[vector * 4 + 2] = handler;
        bus.words[0x100] = opcode; bus.words[0x102] = 0x7209; // MOVEQ #9,D1
        bus.words[0x104] = 0x4e71;
        bus.words[handler] = 0x7007; // MOVEQ #7,D0
        bus.words[handler + 2] = 0x4e73; // RTE
        bus.words[handler + 4] = 0x4e71;
    }

    void checkShortFrame(const Bus &bus, std::uint16_t pc)
    {
        check(bus.words.at(0xffa) == 0x2700, "SR no quadro curto");
        check(bus.words.at(0xffc) == 0 && bus.words.at(0xffe) == pc,
              "PC no quadro curto");
        check(bus.writes.size() == 3, "três palavras no quadro curto");
    }
}

int main()
{
    Bus trap;
    setup(trap, 0x4e43, 35, 0x0200); // TRAP #3
    Moto68000 trapCpu(trap);
    trapCpu.reset();
    run(trapCpu, 40 + 34);
    check(trapCpu.state().pc == 0x200 && trapCpu.state().address[7] == 0xffa,
          "TRAP alcança o tratador em 34 ciclos");
    check(trapCpu.state().instructions == 1, "TRAP conta como instrução concluída");
    checkShortFrame(trap, 0x0102);
    run(trapCpu, 4);
    check(trapCpu.state().data[0] == 7 && trapCpu.state().pc == 0x202,
          "tratador executado");
    const auto readsBeforeReturn = trap.reads.size();
    run(trapCpu, 20);
    check(trapCpu.state().pc == 0x102 && trapCpu.state().address[7] == 0x1000 &&
          trapCpu.state().sr == 0x2700, "RTE restaura PC, SR e pilha em 20 ciclos");
    check(trap.reads.size() == readsBeforeReturn + 5 &&
          trap.reads[readsBeforeReturn] == 0xffa &&
          trap.reads[readsBeforeReturn + 1] == 0xffc &&
          trap.reads[readsBeforeReturn + 2] == 0xffe &&
          trap.reads[readsBeforeReturn + 3] == 0x102 &&
          trap.reads[readsBeforeReturn + 4] == 0x104, "ordem das leituras do RTE");
    run(trapCpu, 4);
    check(trapCpu.state().pc == 0x104 && trapCpu.state().data[1] == 9,
          "programa retomado após TRAP");

    Bus waited;
    setup(waited, 0x4e43, 35, 0x0200);
    waited.waitAddress = 0xffe;
    waited.writeWait = 2;
    Moto68000 waitedCpu(waited);
    waitedCpu.reset();
    run(waitedCpu, 40 + 34 + 2);
    check(waitedCpu.state().pc == 0x200 && waitedCpu.state().address[7] == 0xffa &&
          waited.writes.size() == 3, "espera na pilha prolonga TRAP sem repetir escrita");
    waited.waitAddress = 0xffa;
    waited.readWait = 2;
    run(waitedCpu, 20 + 2);
    check(waitedCpu.state().pc == 0x102 && waitedCpu.state().address[7] == 0x1000,
          "espera na pilha prolonga RTE sem avançar cedo");

    for (const auto [opcode, vector] : {
        std::pair<std::uint16_t, unsigned>{0x4afc, 4},
        {0xa123, 10}, {0xf456, 11}})
    {
        Bus illegal;
        setup(illegal, opcode, vector, 0x0220);
        Moto68000 cpu(illegal);
        cpu.reset();
        run(cpu, 40 + 34);
        check(cpu.state().pc == 0x220 && cpu.state().address[7] == 0xffa,
              "vetor de instrução inválida");
        checkShortFrame(illegal, 0x0100);
    }

    Bus user;
    setup(user, 0x4e73, 32, 0x0200); // RTE inicial carrega SR de usuário.
    user.words[0x1000] = 0; user.words[0x1002] = 0; user.words[0x1004] = 0x0120;
    user.words[0x120] = 0x4e40; // TRAP #0 em modo usuário
    user.words[0x122] = 0x4e73; // RTE em modo usuário: violação de privilégio
    user.words[0x124] = 0x4e71;
    user.words[0x20] = 0; user.words[0x22] = 0x0240;
    user.words[0x240] = 0x4e71; user.words[0x242] = 0x4e71;
    user.words[0x200] = 0x4e73;
    Moto68000 userCpu(user);
    userCpu.reset();
    run(userCpu, 40 + 20);
    check(userCpu.state().pc == 0x120 && userCpu.state().sr == 0 &&
          userCpu.state().address[7] == 0, "RTE troca SSP pelo USP");
    run(userCpu, 34);
    check(userCpu.state().pc == 0x200 && userCpu.state().address[7] == 0x1000 &&
          user.words.at(0x1000) == 0 && user.words.at(0x1004) == 0x0122,
          "TRAP de usuário usa o SSP e salva o SR anterior");
    bool userProgramFetch = false;
    for (const auto &pins : user.trace)
        if (pins.active && pins.address == 0x120 && pins.functionCode == 2)
            userProgramFetch = true;
    check(userProgramFetch, "busca de usuário usa o código de função 2");
    run(userCpu, 20);
    check(userCpu.state().pc == 0x122 && userCpu.state().sr == 0 &&
          userCpu.state().address[7] == 0, "RTE devolve o modo usuário");
    run(userCpu, 34);
    check(userCpu.state().pc == 0x240 && user.words.at(0x1000) == 0 &&
          user.words.at(0x1004) == 0x0122,
          "RTE de usuário aciona violação de privilégio");

    Bus returnFault;
    setup(returnFault, 0x4e73, 2, 0x0300);
    returnFault.words[0x1000] = 0x2700;
    returnFault.words[0x1002] = 0;
    returnFault.words[0x1004] = 0x0120;
    returnFault.words[0x120] = 0x4e71;
    returnFault.failingRead = 0x122;
    Moto68000 returnFaultCpu(returnFault);
    returnFaultCpu.reset();
    run(returnFaultCpu, 40 + 19 + 49);
    check(returnFaultCpu.state().pc == 0x300 && returnFault.words.at(0xffe) == 0x4e73 &&
          returnFault.words.at(0x1002) == 0x0122,
          "erro na pré-busca do RTE conserva a instrução original");
}

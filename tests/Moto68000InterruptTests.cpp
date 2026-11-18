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
        std::vector<M68KPins> trace;
        std::vector<std::uint32_t> writes;
        M68KInterruptResponse response;
        unsigned acknowledgements = 0;
        unsigned acknowledgeWait = 0;
        bool acknowledgeError = false;

        std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte inesperado"); }
        std::uint16_t readWord(std::uint32_t address) override
        {
            return words.count(address) ? words.at(address) : 0x4e71;
        }
        void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("byte inesperado"); }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            writes.push_back(address);
            words[address] = value;
        }
        bool tryAcknowledgeInterrupt(unsigned, M68KInterruptResponse &result) override
        {
            ++acknowledgements;
            if (acknowledgeWait) { --acknowledgeWait; return false; }
            if (acknowledgeError) throw M68KBusError("reconhecimento espúrio");
            result = response;
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

    void setup(Bus &bus, std::uint16_t sr = 0x2000)
    {
        bus.words[0] = 0; bus.words[2] = 0x1000;
        bus.words[4] = 0; bus.words[6] = 0x0100;
        bus.words[0x100] = 0x4e73; // RTE prepara o SR para o programa de teste.
        bus.words[0x102] = 0x4e71;
        bus.words[0x1000] = sr; bus.words[0x1002] = 0; bus.words[0x1004] = 0x0120;
        bus.words[0x120] = bus.words[0x122] = bus.words[0x124] = 0x4e71;
        bus.words[9 * 4] = 0; bus.words[9 * 4 + 2] = 0x0300;
        bus.words[28 * 4] = 0; bus.words[28 * 4 + 2] = 0x0200;
        bus.words[29 * 4] = 0; bus.words[29 * 4 + 2] = 0x0200;
        bus.words[30 * 4] = 0; bus.words[30 * 4 + 2] = 0x0200;
        bus.words[31 * 4] = 0; bus.words[31 * 4 + 2] = 0x0220;
        bus.words[24 * 4] = 0; bus.words[24 * 4 + 2] = 0x0400;
        bus.words[0x50 * 4] = 0; bus.words[0x50 * 4 + 2] = 0x0500;
        bus.words[32 * 4] = 0; bus.words[32 * 4 + 2] = 0x0600;
        bus.words[4 * 4] = 0; bus.words[4 * 4 + 2] = 0x0700;
        bus.words[3 * 4] = 0; bus.words[3 * 4 + 2] = 0x0700;
        bus.words[0x200] = bus.words[0x220] = bus.words[0x300] = 0x4e73;
        bus.words[0x400] = bus.words[0x500] = bus.words[0x600] = 0x4e73;
        bus.words[0x700] = 0x4e71;
    }

    void prepare(Moto68000 &cpu)
    {
        cpu.reset();
        run(cpu, 60);
        check(cpu.state().pc == 0x120, "preparação do SR por RTE");
    }

    void checkFrame(const Bus &bus, std::uint16_t sr, std::uint16_t pc)
    {
        check(bus.words.at(0x1000) == sr && bus.words.at(0x1002) == 0 &&
              bus.words.at(0x1004) == pc, "quadro curto da interrupção");
    }
}

int main()
{
    Bus autoBus;
    setup(autoBus);
    Moto68000 autoCpu(autoBus);
    prepare(autoCpu);
    autoCpu.setInterruptLevel(5);
    run(autoCpu, 44);
    check(autoCpu.state().pc == 0x200 && autoCpu.state().address[7] == 0x1000 &&
          autoCpu.state().sr == 0x2500, "autovetor de nível 5 em 44 ciclos");
    checkFrame(autoBus, 0x2000, 0x0120);
    check(autoBus.acknowledgements == 1 && autoBus.writes.size() == 3,
          "um reconhecimento e três escritas na pilha");
    bool sawAck = false, sawVectorData = false, sawHandlerProgram = false;
    for (const auto &pins : autoBus.trace)
    {
        if (pins.active && pins.address == 29 * 4 && pins.functionCode == 5)
            sawVectorData = true;
        if (pins.active && pins.address == 0x200 && pins.functionCode == 6)
            sawHandlerProgram = true;
        if (pins.interruptAcknowledge)
        {
            sawAck = true;
            check(pins.active && pins.functionCode == 7 && pins.interruptLevel == 5 &&
                  pins.address == 0xfffffb && !pins.write,
                  "pinos do reconhecimento de interrupção");
            if (pins.phase >= 1)
                check(pins.upperDataStrobe && pins.lowerDataStrobe,
                      "ambas as linhas de dados no reconhecimento");
        }
    }
    check(sawAck && sawVectorData && sawHandlerProgram,
          "reconhecimento, vetor em dados e tratador em programa");
    run(autoCpu, 20 + 44);
    check(autoCpu.state().pc == 0x200 && autoBus.acknowledgements == 2,
          "nível mantido gera novo pedido após restaurar a máscara");
    autoCpu.setInterruptLevel(0);
    run(autoCpu, 20 + 4);
    check(autoCpu.state().pc == 0x122 && autoCpu.state().address[7] == 0x1006,
          "RTE retoma o programa interrompido");

    Bus masked;
    setup(masked, 0x2500);
    Moto68000 maskedCpu(masked);
    prepare(maskedCpu);
    maskedCpu.setInterruptLevel(4);
    run(maskedCpu, 4);
    check(maskedCpu.state().pc == 0x122 && masked.acknowledgements == 0,
          "máscara bloqueia nível menor");
    maskedCpu.setInterruptLevel(6);
    run(maskedCpu, 44);
    check(maskedCpu.state().pc == 0x200 && maskedCpu.state().sr == 0x2600,
          "nível superior à máscara é aceito");
    checkFrame(masked, 0x2500, 0x0122);

    Bus delayed;
    setup(delayed);
    delayed.words[0x120] = 0x6004; // BRA para $126.
    delayed.words[0x126] = 0x4e71;
    Moto68000 delayedCpu(delayed);
    prepare(delayedCpu);
    run(delayedCpu, 2);
    delayedCpu.setInterruptLevel(4);
    run(delayedCpu, 8);
    check(delayedCpu.state().pc == 0x126 && delayed.acknowledgements == 0,
          "interrupção espera o fim do desvio");
    run(delayedCpu, 44);
    check(delayedCpu.state().pc == 0x200 && delayed.words.at(0x1004) == 0x0126,
          "PC salvo após conclusão da instrução");

    Bus edge;
    setup(edge);
    edge.words[0x100] = edge.words[0x102] = 0x4e71;
    Moto68000 edgeCpu(edge);
    edgeCpu.reset();
    run(edgeCpu, 40);
    edgeCpu.setInterruptLevel(7);
    run(edgeCpu, 44);
    check(edgeCpu.state().pc == 0x220 && edgeCpu.state().sr == 0x2700,
          "subida do nível 7 ignora máscara 7");
    check(edge.words.at(0xffa) == 0x2700 && edge.words.at(0xffc) == 0 &&
          edge.words.at(0xffe) == 0x0100, "quadro do nível 7");
    run(edgeCpu, 20 + 4);
    check(edgeCpu.state().pc == 0x102 && edge.acknowledgements == 1,
          "nível 7 mantido alto não repete a interrupção");
    edgeCpu.setInterruptLevel(0);
    edgeCpu.setInterruptLevel(7);
    run(edgeCpu, 44);
    check(edgeCpu.state().pc == 0x220 && edge.acknowledgements == 2,
          "nova subida do nível 7 gera outra interrupção");

    Bus device;
    setup(device);
    device.response.kind = M68KInterruptResponse::Kind::DeviceVector;
    device.response.vector = 0x50;
    device.acknowledgeWait = 2;
    Moto68000 deviceCpu(device);
    prepare(deviceCpu);
    deviceCpu.setInterruptLevel(4);
    run(deviceCpu, 44 + 2);
    check(deviceCpu.state().pc == 0x500 && device.acknowledgements == 3,
          "vetor fornecido pelo dispositivo com duas esperas");
    bool sawData = false, sawWait = false;
    for (const auto &pins : device.trace)
    {
        if (pins.interruptAcknowledge && pins.dataValid && pins.data == 0x50)
            sawData = true;
        if (pins.interruptAcknowledge && pins.waiting)
            sawWait = true;
    }
    check(sawData && sawWait, "vetor externo e espera visíveis nos pinos");

    Bus spurious;
    setup(spurious);
    spurious.acknowledgeError = true;
    Moto68000 spuriousCpu(spurious);
    prepare(spuriousCpu);
    spuriousCpu.setInterruptLevel(4);
    run(spuriousCpu, 44);
    check(spuriousCpu.state().pc == 0x400 && !spuriousCpu.halted() &&
          spuriousCpu.state().sr == 0x2400, "BERR no reconhecimento usa vetor espúrio");

    bool invalidLevel = false;
    try { autoCpu.setInterruptLevel(8); }
    catch (const std::invalid_argument &) { invalidLevel = true; }
    check(invalidLevel, "níveis IPL válidos são 0..7");

    Bus traced;
    setup(traced, 0xa000); // Modo supervisor com bit T ativo.
    Moto68000 traceCpu(traced);
    prepare(traceCpu);
    run(traceCpu, 4 + 34);
    check(traceCpu.state().pc == 0x300 && traceCpu.state().sr == 0x2000 &&
          traceCpu.state().address[7] == 0x1000,
          "NOP seguido de trace em 34 ciclos");
    checkFrame(traced, 0xa000, 0x0122);
    run(traceCpu, 20);
    check(traceCpu.state().pc == 0x122 && traceCpu.state().sr == 0xa000,
          "RTE do tratador restaura T");
    run(traceCpu, 4 + 34);
    check(traceCpu.state().pc == 0x300 && traced.words.at(0x1004) == 0x0124,
          "trace acompanha a instrução seguinte");

    Bus userTrace;
    setup(userTrace, 0x8000); // T ativo em modo usuário.
    Moto68000 userTraceCpu(userTrace);
    prepare(userTraceCpu);
    check(userTraceCpu.state().address[7] == 0, "USP após retorno ao usuário");
    run(userTraceCpu, 4 + 34);
    check(userTraceCpu.state().pc == 0x300 && userTraceCpu.state().sr == 0x2000 &&
          userTraceCpu.state().address[7] == 0x1000 &&
          userTrace.words.at(0x1000) == 0x8000 && userTrace.words.at(0x1004) == 0x0122,
          "trace de usuário troca para SSP e preserva SR");
    run(userTraceCpu, 20);
    check(userTraceCpu.state().pc == 0x122 && userTraceCpu.state().sr == 0x8000 &&
          userTraceCpu.state().address[7] == 0, "RTE devolve o USP após trace");

    Bus tracedBranch;
    setup(tracedBranch, 0xa000);
    tracedBranch.words[0x120] = 0x6004; // BRA para $126.
    tracedBranch.words[0x126] = 0x4e71;
    Moto68000 branchCpu(tracedBranch);
    prepare(branchCpu);
    run(branchCpu, 10 + 34);
    check(branchCpu.state().pc == 0x300 && tracedBranch.words.at(0x1004) == 0x0126,
          "trace aguarda o fim do desvio");

    Bus tracedMemory;
    setup(tracedMemory, 0xa000);
    tracedMemory.words[0x120] = 0x41f8; // LEA $0400.W,A0
    tracedMemory.words[0x122] = 0x0400;
    Moto68000 memoryCpu(tracedMemory);
    prepare(memoryCpu);
    run(memoryCpu, 8 + 34);
    check(memoryCpu.state().pc == 0x300 && memoryCpu.state().address[0] == 0x400 &&
          tracedMemory.words.at(0x1004) == 0x0124,
          "trace aguarda o fim de instrução com extensão");

    Bus tracedReturn;
    setup(tracedReturn, 0xa000);
    tracedReturn.words[0x120] = 0x4e73;
    tracedReturn.words[0x1006] = 0x2000;
    tracedReturn.words[0x1008] = 0;
    tracedReturn.words[0x100a] = 0x0140;
    tracedReturn.words[0x140] = 0x4e71;
    Moto68000 returnCpu(tracedReturn);
    prepare(returnCpu);
    run(returnCpu, 20 + 34);
    check(returnCpu.state().pc == 0x300 && returnCpu.state().sr == 0x2000 &&
          tracedReturn.words.at(0x100a) == 0x0140,
          "RTE iniciado com T ativo gera trace mesmo restaurando T limpo");

    Bus priority;
    setup(priority, 0xa000);
    Moto68000 priorityCpu(priority);
    prepare(priorityCpu);
    run(priorityCpu, 4); // NOP conclui com T ativo.
    priorityCpu.setInterruptLevel(4);
    run(priorityCpu, 34);
    check(priorityCpu.state().pc == 0x300 && priority.acknowledgements == 0,
          "trace precede interrupção simultânea");
    run(priorityCpu, 44);
    check(priorityCpu.state().pc == 0x200 && priority.words.at(0xffa) == 0x2000 &&
          priority.words.at(0xffe) == 0x0300,
          "interrupção interrompe antes do tratador de trace");
    priorityCpu.setInterruptLevel(0);
    run(priorityCpu, 20 + 20);
    check(priorityCpu.state().pc == 0x122 && priorityCpu.state().sr == 0xa000,
          "dois RTEs retomam o programa rastreado");

    Bus trapped;
    setup(trapped, 0xa000);
    trapped.words[0x120] = 0x4e40; // TRAP #0 sob rastreamento.
    Moto68000 trappedCpu(trapped);
    prepare(trappedCpu);
    run(trappedCpu, 34);
    check(trappedCpu.state().pc == 0x600, "TRAP tem precedência sobre trace");
    run(trappedCpu, 34);
    check(trappedCpu.state().pc == 0x300 && trapped.words.at(0xffe) == 0x0600,
          "trace segue o TRAP antes de executar seu tratador");
    run(trappedCpu, 20 + 20);
    check(trappedCpu.state().pc == 0x122 && trappedCpu.state().sr == 0xa000,
          "retornos de trace e TRAP preservam a ordem");

    Bus illegal;
    setup(illegal, 0xa000);
    illegal.words[0x120] = 0x4afc;
    Moto68000 illegalCpu(illegal);
    prepare(illegalCpu);
    run(illegalCpu, 34 + 4);
    check(illegalCpu.state().pc == 0x702 && illegalCpu.state().address[7] == 0x1000,
          "instrução ilegal não gera trace");

    Bus aborted;
    setup(aborted, 0xa000);
    aborted.words[0x120] = 0x6001; // Desvio para endereço ímpar.
    Moto68000 abortedCpu(aborted);
    prepare(abortedCpu);
    run(abortedCpu, 2 + 50 + 4);
    check(abortedCpu.state().pc == 0x702 && abortedCpu.state().sr == 0x2000,
          "instrução abortada por erro de endereço não gera trace");
}

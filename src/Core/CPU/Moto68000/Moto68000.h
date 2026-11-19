#pragma once

#include <array>
#include <cstdint>
#include "Core/Bus/M68KBus.h"

namespace R2MEGA::Core
{
    // Base do 68000: reset, fila de pré-busca e primeiras instruções por ciclos de CPU.
    class Moto68000
    {
    public:
        struct State
        {
            std::array<std::uint32_t, 8> data{};
            std::array<std::uint32_t, 8> address{};
            std::uint32_t pc = 0;
            std::array<std::uint16_t, 2> prefetch{};
            std::uint64_t instructions = 0;
            std::uint16_t sr = 0x2700;
            std::uint64_t cycles = 0;
        };

        explicit Moto68000(M68KBus &bus);
        Moto68000(const Moto68000 &) = delete;
        Moto68000 &operator=(const Moto68000 &) = delete;

        // Inicia a leitura do SSP e do PC iniciais; não apaga a memória externa.
        void reset();
        // Avança um ciclo da CPU. Uma leitura pode permanecer pendente por esperas.
        void clock();
        // Nível presente nas entradas IPL; zero remove o pedido.
        void setInterruptLevel(unsigned level);
        const State &state() const { return state_; }
        const M68KPins &pins() const { return pins_; }
        // Indica apenas que os quatro words dos vetores foram lidos.
        bool resetVectorsLoaded() const { return active_ && resetRead_ >= 4; }
        bool resetComplete() const { return active_ && resetRead_ == 6; }
        bool halted() const { return halted_; }

    private:
        enum class TransferResult
        {
            Waiting,
            Complete,
            Fault
        };
        TransferResult transfer(std::uint32_t address, bool byte, bool write,
                                std::uint16_t value, unsigned functionCode);
        void beginException(unsigned vector, std::uint32_t savedPc, bool longFrame,
                            bool instructionContext = false);
        void beginFault(unsigned vector, std::uint32_t address, bool write,
                        unsigned functionCode);
        void clockException();
        void clockInterruptAcknowledge();
        void clockReturn();
        void clockControl();
        void clockExtendedMove();
        // A referência não é proprietária; o barramento deve sobreviver à CPU.
        M68KBus &bus_;
        State state_;
        bool active_ = false;
        unsigned resetRead_ = 6;
        unsigned startupCycles_ = 0;
        unsigned busPhase_ = 0;
        std::uint16_t pendingWord_ = 0;
        M68KPins pins_;
        bool branchActive_ = false;
        unsigned branchIdle_ = 0;
        unsigned branchRead_ = 0;
        std::uint32_t branchTarget_ = 0;
        unsigned branchReadsNeeded_ = 2;
        unsigned branchIdleNeeded_ = 2;
        bool branchTaken_ = true;
        bool quickLongActive_ = false;
        unsigned quickIdle_ = 0;
        enum class ControlOp
        {
            None,
            Bsr,
            Jsr,
            Jmp,
            Rts
        };
        ControlOp controlOp_ = ControlOp::None;
        unsigned controlStage_ = 0;
        unsigned controlIdle_ = 0;
        bool controlExtensionPending_ = false;
        std::uint32_t controlTarget_ = 0;
        std::uint32_t controlReturn_ = 0;
        std::uint16_t controlIr_ = 0;
        enum class MemoryOp
        {
            None,
            Lea,
            Load,
            Store,
            ExtendedMove
        };
        MemoryOp memoryOp_ = MemoryOp::None;
        unsigned memoryStage_ = 0;
        std::uint16_t memoryPrefetch_ = 0;
        std::uint32_t memoryValue_ = 0;
        unsigned memorySize_ = 2;
        unsigned memoryMode_ = 2;
        unsigned memoryIdle_ = 0;
        std::uint32_t memoryAddress_ = 0;
        bool memoryAddressReady_ = false;
        // Mantém as extensões e a pré-busca enquanto o MOVE acessa o barramento.
        struct ExtendedMove
        {
            unsigned size = 2;
            unsigned mode = 0;
            unsigned reg = 0;
            unsigned dataReg = 0;
            unsigned extensions = 1;
            unsigned stage = 0;
            bool store = false;
            bool immediate = false;
            std::uint16_t firstExtension = 0;
            std::uint16_t nextOpcode = 0;
            std::uint16_t nextFollowing = 0;
            std::uint32_t address = 0;
            std::uint32_t value = 0;
        } extendedMove_;
        std::uint8_t pendingByte_ = 0;
        bool halted_ = false;
        bool exceptionActive_ = false;
        bool exceptionLongFrame_ = false;
        bool exceptionInstructionContext_ = false;
        bool interruptException_ = false;
        bool interruptAcknowledged_ = false;
        unsigned acknowledgedLevel_ = 0;
        M68KInterruptResponse interruptResponse_;
        unsigned exceptionIdle_ = 0;
        unsigned exceptionStage_ = 0;
        unsigned exceptionVector_ = 0;
        std::uint32_t exceptionPc_ = 0;
        std::uint32_t exceptionAddress_ = 0;
        std::uint32_t exceptionTarget_ = 0;
        std::uint16_t exceptionSr_ = 0;
        std::uint16_t exceptionIr_ = 0;
        std::uint16_t exceptionStatus_ = 0;
        bool returnActive_ = false;
        unsigned returnStage_ = 0;
        std::uint16_t returnSr_ = 0;
        std::uint16_t returnIr_ = 0;
        std::uint32_t returnPc_ = 0;
        std::uint32_t ssp_ = 0;
        std::uint32_t usp_ = 0;
        unsigned interruptLevel_ = 0;
        bool level7EdgePending_ = false;
        bool traceInstruction_ = false;
        bool tracePending_ = false;
        bool traceDeferred_ = false;
    };
}
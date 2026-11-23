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

        // Guarda a causa original mesmo depois que o tratador da ROM assume o PC.
        struct ExceptionEvent
        {
            std::uint64_t serial = 0;
            unsigned vector = 0;
            std::uint32_t pc = 0;
            std::uint16_t opcode = 0;
            std::uint32_t address = 0;
            bool addressValid = false;
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
        // Restaura um estado entre instruções, com a fila de pré-busca já carregada.
        void restoreInstructionBoundary(const State &state, std::uint32_t usp, std::uint32_t ssp);
        bool atInstructionBoundary() const;
        std::uint32_t userStackPointer() const { return state_.sr & 0x2000 ? usp_ : state_.address[7]; }
        std::uint32_t supervisorStackPointer() const { return state_.sr & 0x2000 ? state_.address[7] : ssp_; }
        const State &state() const { return state_; }
        const M68KPins &pins() const { return pins_; }
        const ExceptionEvent &lastException() const { return lastException_; }
        // Indica apenas que os quatro words dos vetores foram lidos.
        bool resetVectorsLoaded() const { return active_ && resetRead_ >= 4; }
        bool resetComplete() const { return active_ && resetRead_ == 6; }
        bool halted() const { return halted_; }
        bool stopped() const { return stopped_; }

    private:
        enum class TransferResult
        {
            Waiting,
            Complete,
            Fault
        };
        TransferResult transfer(std::uint32_t address, bool byte, bool write,
                                std::uint16_t value, unsigned functionCode,
                                bool clockAddressError = false);
        void beginException(unsigned vector, std::uint32_t savedPc, bool longFrame,
                            bool instructionContext = false);
        void beginFault(unsigned vector, std::uint32_t address, bool write,
                        unsigned functionCode);
        void clockException();
        void clockInterruptAcknowledge();
        void clockReturn();
        void clockControl();
        void clockSystemInstruction();
        void clockExtendedMove();
        void clockCombinedMove();
        void finishCombinedMove();
        void clockTestClearCompare();
        void clockArithmetic();
        void clockMultiplyDivide();
        void clockExtendedArithmetic();
        void clockStatusImmediate();
        void clockBitOperation();
        void clockRegisterOperation();
        void clockShiftRotate();
        void clockMovep();
        void clockMovem();
        void clockStatusMove();
        void clockFrame();
        void clockScc();
        // A referência não é proprietária; o barramento deve sobreviver à CPU.
        M68KBus &bus_;
        State state_;
        ExceptionEvent lastException_;
        bool active_ = false;
        unsigned resetRead_ = 6;
        unsigned startupCycles_ = 0;
        unsigned busPhase_ = 0;
        std::uint16_t pendingWord_ = 0;
        M68KPins pins_;
        bool branchActive_ = false;
        bool sccActive_ = false;
        unsigned sccStage_ = 0;
        unsigned sccMode_ = 0, sccReg_ = 0, sccLength_ = 2;
        std::uint32_t sccAddress_ = 0;
        std::uint8_t sccValue_ = 0;
        std::uint16_t sccNextOpcode_ = 0;
        unsigned branchIdle_ = 0;
        unsigned branchRead_ = 0;
        std::uint32_t branchTarget_ = 0;
        unsigned branchReadsNeeded_ = 2;
        unsigned branchIdleNeeded_ = 2;
        bool branchTaken_ = true;
        bool branchDbccExpired_ = false;
        bool quickLongActive_ = false;
        unsigned quickIdle_ = 0;
        enum class ControlOp
        {
            None,
            Bsr,
            Jsr,
            Jmp,
            Rts,
            Pea,
            Reset,
            Stop,
            TrapOverflow
        };
        ControlOp controlOp_ = ControlOp::None;
        unsigned controlStage_ = 0;
        unsigned controlIdle_ = 0;
        bool controlExtensionPending_ = false;
        std::uint32_t controlTarget_ = 0;
        std::uint32_t controlReturn_ = 0;
        std::uint16_t controlIr_ = 0;
        std::uint16_t controlNextOpcode_ = 0;
        unsigned controlExtensions_ = 0;
        enum class MemoryOp
        {
            None,
            Lea,
            Load,
            Store,
            ExtendedMove,
            CombinedMove,
            TestClearCompare,
            Arithmetic,
            MultiplyDivide,
            ExtendedArithmetic,
            StatusImmediate,
            BitOperation,
            RegisterOperation,
            ShiftRotate,
            Movep,
            Movem,
            StatusMove,
            Frame
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
        // Separa as extensões da origem e do destino na ordem em que chegam da pré-busca.
        struct CombinedMove
        {
            enum class Phase
            {
                SourceExtensions,
                SourceIdle,
                SourceRead,
                TargetExtensions,
                PrefetchBeforeWrite,
                Write,
                PrefetchAfterWrite
            };
            Phase phase = Phase::SourceExtensions;
            unsigned size = 2;
            unsigned sourceMode = 0, sourceReg = 0;
            unsigned targetMode = 0, targetReg = 0;
            unsigned sourceExtensions = 0, targetExtensions = 0;
            unsigned sourceFetchesLeft = 0, targetFetchesLeft = 0;
            unsigned streamIndex = 2;
            unsigned sourceReads = 0, targetWrites = 0;
            unsigned sourceIdle = 0, targetIdle = 0;
            bool immediate = false;
            bool specialAbsoluteLong = false;
            bool sourceAddressReady = false, targetAddressReady = false;
            std::array<std::uint16_t, 2> sourceExtension{};
            std::array<std::uint16_t, 2> targetExtension{};
            std::uint16_t nextOpcode = 0, nextFollowing = 0;
            std::uint32_t sourceAddress = 0, targetAddress = 0, value = 0;
        } combinedMove_;
        // Conserva extensões, leitura e pré-busca de teste, limpeza e comparação.
        struct TestClearCompare
        {
            enum class Kind
            {
                Test,
                TestSet,
                Clear,
                Compare,
                CompareAddress
            };
            enum class Phase
            {
                Extensions,
                Idle,
                Read,
                Prefetch,
                Write
            };
            Kind kind = Kind::Test;
            Phase phase = Phase::Extensions;
            unsigned size = 2, mode = 0, reg = 0, destination = 0;
            unsigned extensions = 0, fetchesLeft = 0, streamIndex = 2;
            unsigned idle = 0, reads = 0, writes = 0;
            bool addressReady = false;
            std::array<std::uint16_t, 2> extension{};
            std::uint16_t nextOpcode = 0, nextFollowing = 0;
            std::uint32_t address = 0, value = 0;
        } testClearCompare_;
        // Guarda operandos e extensões até terminar a sequência de ADD/SUB.
        struct Arithmetic
        {
            enum class Logic
            {
                None,
                And,
                Or,
                Xor
            };
            enum class Unary
            {
                None,
                Negate,
                NegateExtend,
                NegateDecimal,
                Complement
            };
            enum class Phase
            {
                Extensions,
                Idle,
                Read,
                Internal,
                Prefetch,
                FinalIdle,
                Write
            };
            Phase phase = Phase::Extensions;
            Logic logic = Logic::None;
            Unary unary = Unary::None;
            bool subtract = false, addressDestination = false, memoryDestination = false;
            bool immediateInstruction = false, quickInstruction = false, compareOnly = false;
            bool checkBounds = false;
            unsigned size = 2, mode = 0, reg = 0, dataReg = 0;
            unsigned quickCount = 0;
            unsigned extensions = 0, immediateWords = 0, fetchesLeft = 0, streamIndex = 2;
            unsigned leadingIdle = 0, idle = 0, reads = 0, writes = 0;
            bool addressReady = false;
            std::array<std::uint16_t, 4> extension{};
            std::uint16_t nextOpcode = 0, nextFollowing = 0;
            std::uint16_t flags = 0;
            std::uint32_t address = 0, operand = 0, result = 0;
        } arithmetic_;
        // Conserva a origem e a temporização variável das multiplicações e divisões.
        struct MultiplyDivide
        {
            enum class Phase
            {
                Extensions,
                AddressIdle,
                Read,
                Internal,
                Prefetch
            };
            Phase phase = Phase::Extensions;
            bool divide = false, signedOperation = false, overflow = false;
            unsigned mode = 0, reg = 0, destination = 0;
            unsigned extensions = 0, fetchesLeft = 0, streamIndex = 2;
            unsigned idle = 0;
            bool addressReady = false;
            std::array<std::uint16_t, 2> extension{};
            std::uint16_t nextOpcode = 0, nextFollowing = 0, source = 0;
            std::uint32_t address = 0, result = 0;
        } multiplyDivide_;
        // ADDX/SUBX, ABCD/SBCD e CMPM leem a origem antes do destino.
        struct ExtendedArithmetic
        {
            enum class Phase
            {
                Idle,
                SourceRead,
                TargetRead,
                Prefetch,
                FinalIdle,
                Write
            };
            Phase phase = Phase::Idle;
            bool subtract = false, memory = false, compareMemory = false, decimal = false;
            unsigned size = 2, sourceReg = 0, targetReg = 0;
            unsigned idle = 0, sourceReads = 0, targetReads = 0, writes = 0;
            bool sourceAddressReady = false, targetAddressReady = false;
            std::uint16_t nextOpcode = 0, nextFollowing = 0, flags = 0;
            std::uint32_t sourceAddress = 0, targetAddress = 0;
            std::uint32_t source = 0, target = 0, result = 0;
        } extendedArithmetic_;
        // Mantém a operação em CCR/SR até refazer a fila de pré-busca.
        struct StatusImmediate
        {
            enum class Phase
            {
                FirstFetch,
                Idle,
                OpcodeFetch,
                FollowingFetch
            };
            Phase phase = Phase::FirstFetch;
            unsigned idle = 8;
            std::uint16_t opcode = 0, immediate = 0;
            std::uint16_t nextOpcode = 0, nextFollowing = 0;
        } statusImmediate_;
        // Conserva o bit original até concluir a escrita e atualizar somente Z.
        struct BitOperation
        {
            enum class Phase
            {
                Extensions,
                Idle,
                Read,
                Prefetch,
                Write
            };
            Phase phase = Phase::Extensions;
            unsigned kind = 0, mode = 0, reg = 0, bitReg = 0;
            unsigned extensions = 0, fetchesLeft = 0, streamIndex = 2;
            unsigned idle = 0, bit = 0;
            bool staticBit = false, addressReady = false, originalZero = false;
            std::array<std::uint16_t, 3> extension{};
            std::uint16_t nextOpcode = 0, nextFollowing = 0;
            std::uint32_t address = 0, value = 0, result = 0;
        } bitOperation_;
        // A troca de registradores usa dois ciclos internos antes da pré-busca.
        struct RegisterOperation
        {
            unsigned idle = 0;
        } registerOperation_;
        // Guarda a contagem antes da operação, inclusive quando origem e destino são o mesmo Dn.
        struct ShiftRotate
        {
            enum class Phase
            {
                Extensions,
                Idle,
                Read,
                Prefetch,
                Write
            };
            Phase phase = Phase::Extensions;
            bool memory = false, addressReady = false;
            unsigned idle = 0, count = 0, mode = 0, reg = 0;
            unsigned extensions = 0, fetchesLeft = 0, streamIndex = 2;
            std::array<std::uint16_t, 2> extension{};
            std::uint16_t nextOpcode = 0, nextFollowing = 0;
            std::uint16_t result = 0, flags = 0;
            std::uint32_t address = 0;
        } shiftRotate_;
        // MOVEP acessa bytes alternados, sempre do mais significativo ao menos significativo.
        struct Movep
        {
            enum class Phase
            {
                NextOpcode,
                Bytes,
                FollowingFetch
            };
            Phase phase = Phase::NextOpcode;
            bool write = false, longSize = false;
            unsigned dataReg = 0, addressReg = 0, bytesDone = 0;
            std::uint32_t address = 0, value = 0;
            std::uint16_t nextOpcode = 0;
        } movep_;
        // MOVEM conserva a máscara e a ordem dos acessos durante esperas do barramento.
        struct Movem
        {
            enum class Phase
            {
                Extensions,
                NextOpcode,
                Idle,
                Read,
                Write,
                TrailingRead,
                FollowingFetch
            };
            Phase phase = Phase::Extensions;
            unsigned mode = 0, reg = 0, size = 2, extensions = 0;
            unsigned extensionReads = 0, idle = 0;
            unsigned registerIndex = 0, word = 0;
            bool write = false, writeAddressReady = false;
            std::uint16_t mask = 0, extension[2]{}, nextOpcode = 0;
            std::uint32_t address = 0, value = 0, baseValue = 0;
        } movem_;
        // Conserva a origem e refaz a pré-busca após escrever em CCR ou SR.
        struct StatusMove
        {
            enum class Phase
            {
                Extensions,
                Read,
                Idle,
                FirstFetch,
                FollowingFetch,
                Write
            };
            Phase phase = Phase::Extensions;
            bool fromSr = false, toSr = false, addressReady = false;
            unsigned mode = 0, reg = 0, extensions = 0;
            unsigned fetchesLeft = 0, streamIndex = 2, idle = 0;
            unsigned addressIdle = 0;
            std::array<std::uint16_t, 2> extension{};
            std::uint16_t value = 0, nextOpcode = 0, nextFollowing = 0;
            std::uint32_t address = 0;
        } statusMove_;
        // LINK e UNLK conservam os endereços durante as transferências da pilha.
        struct Frame
        {
            bool unlink = false;
            unsigned reg = 0, stage = 0;
            std::uint32_t oldAddress = 0, base = 0, value = 0;
            std::uint16_t nextOpcode = 0;
        } frame_;
        std::uint8_t pendingByte_ = 0;
        bool halted_ = false;
        bool stopped_ = false;
        bool exceptionActive_ = false;
        bool exceptionLongFrame_ = false;
        bool exceptionShortSequence_ = false;
        bool exceptionAccurateAddress_ = false;
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
        bool returnCcrOnly_ = false;
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
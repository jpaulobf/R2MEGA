#include "Core/CPU/Moto68000/Moto68000.h"
#include "Core/Bus/M68KBus.h"
#include <stdexcept>

namespace R2MEGA::Core
{
    namespace
    {
        int signedWord(std::uint16_t value)
        {
            return value & 0x8000 ? int(value) - 0x10000 : int(value);
        }

        bool branchCondition(unsigned condition, std::uint16_t sr)
        {
            const bool n = (sr & 8) != 0, z = (sr & 4) != 0;
            const bool v = (sr & 2) != 0, c = (sr & 1) != 0;
            switch (condition)
            {
            case 0:
                return true; // BRA
            case 2:
                return !c && !z; // HI
            case 3:
                return c || z; // LS
            case 4:
                return !c; // CC
            case 5:
                return c; // CS
            case 6:
                return !z; // NE
            case 7:
                return z; // EQ
            case 8:
                return !v; // VC
            case 9:
                return v; // VS
            case 10:
                return !n; // PL
            case 11:
                return n; // MI
            case 12:
                return n == v; // GE
            case 13:
                return n != v; // LT
            case 14:
                return !z && n == v; // GT
            case 15:
                return z || n != v; // LE
            default:
                throw std::logic_error("68000 branch condition not implemented");
            }
        }

        bool extendedMoveOpcode(std::uint16_t opcode)
        {
            if ((opcode >> 12) < 1 || (opcode >> 12) > 3)
                return false;
            const unsigned sourceMode = (opcode >> 3) & 7, sourceReg = opcode & 7;
            const unsigned targetMode = (opcode >> 6) & 7, targetReg = (opcode >> 9) & 7;
            const bool extendedSource = sourceMode == 5 ||
                                        (sourceMode == 7 && (sourceReg <= 1 || sourceReg == 4));
            const bool extendedTarget = targetMode == 5 ||
                                        (targetMode == 7 && targetReg <= 1);
            return (targetMode == 0 && extendedSource) ||
                   (sourceMode == 0 && extendedTarget);
        }
    }

    Moto68000::Moto68000(M68KBus &bus) : bus_(bus) {}

    void Moto68000::setInterruptLevel(unsigned level)
    {
        if (level > 7)
            throw std::invalid_argument("Nível de interrupção do 68000 fora de 0..7");
        if (level == 7 && interruptLevel_ != 7)
            level7EdgePending_ = true;
        interruptLevel_ = level;
    }

    Moto68000::TransferResult Moto68000::transfer(std::uint32_t address, bool byte,
                                                  bool write, std::uint16_t value, unsigned functionCode)
    {
        const auto busAddress = address & 0x00ffffff;
        if (!byte && (busAddress & 1))
        {
            bus_.observe(pins_);
            if ((exceptionActive_ && exceptionLongFrame_) || !resetComplete())
                halted_ = true;
            else
                beginFault(3, address, write, functionCode);
            return TransferResult::Fault;
        }

        pins_.active = true;
        pins_.phase = busPhase_;
        pins_.address = busAddress;
        pins_.functionCode = functionCode;
        pins_.write = write;
        if (write)
        {
            pins_.data = byte && !(busAddress & 1) ? static_cast<std::uint16_t>((value & 0xff) << 8) : value;
            pins_.dataValid = busPhase_ >= 1;
        }
        pins_.addressStrobe = busPhase_ >= 1;
        pins_.upperDataStrobe = busPhase_ >= 1 && (!byte || !(busAddress & 1));
        pins_.lowerDataStrobe = busPhase_ >= 1 && (!byte || (busAddress & 1));

        // S4/S5: DTACK pode prolongar esta fase sem repetir uma transferência concluída.
        if (busPhase_ == 2)
        {
            try
            {
                const bool ready = write
                                       ? (byte ? bus_.tryWriteByte(busAddress, static_cast<std::uint8_t>(value))
                                               : bus_.tryWriteWord(busAddress, value))
                                       : (byte ? bus_.tryReadByte(busAddress, pendingByte_)
                                               : bus_.tryReadWord(busAddress, pendingWord_));
                if (!ready)
                {
                    pins_.waiting = true;
                    bus_.observe(pins_);
                    return TransferResult::Waiting;
                }
            }
            catch (const M68KBusError &)
            {
                bus_.observe(pins_);
                if ((exceptionActive_ && exceptionLongFrame_) || !resetComplete())
                    halted_ = true;
                else
                    beginFault(2, address, write, functionCode);
                return TransferResult::Fault;
            }
        }
        if (busPhase_ != 3)
        {
            ++busPhase_;
            bus_.observe(pins_);
            return TransferResult::Waiting;
        }
        if (!write)
        {
            pins_.data = byte ? static_cast<std::uint16_t>((busAddress & 1) ? pendingByte_ : pendingByte_ << 8)
                              : pendingWord_;
            pins_.dataValid = true;
        }
        bus_.observe(pins_);
        busPhase_ = 0;
        return TransferResult::Complete;
    }

    void Moto68000::beginException(unsigned vector, std::uint32_t savedPc,
                                   bool longFrame, bool instructionContext)
    {
        exceptionActive_ = true;
        exceptionLongFrame_ = longFrame;
        exceptionInstructionContext_ = instructionContext;
        interruptException_ = false;
        interruptAcknowledged_ = false;
        interruptResponse_ = {};
        exceptionIdle_ = 5;
        exceptionStage_ = 0;
        exceptionVector_ = vector;
        exceptionPc_ = savedPc;
        exceptionTarget_ = 0;
        exceptionSr_ = state_.sr;
        exceptionIr_ = state_.prefetch[0];
        if (!(state_.sr & 0x2000))
        {
            usp_ = state_.address[7];
            state_.address[7] = ssp_;
        }
        // A exceção usa o SSP e limpa o bit de rastreamento.
        state_.sr = (state_.sr | 0x2000) & ~std::uint16_t(0x8000);
        branchActive_ = quickLongActive_ = returnActive_ = false;
        controlOp_ = ControlOp::None;
        memoryOp_ = MemoryOp::None;
        traceInstruction_ = tracePending_ = false;
        busPhase_ = 0;
    }

    void Moto68000::beginFault(unsigned vector, std::uint32_t address, bool write,
                               unsigned functionCode)
    {
        const bool processingException = exceptionActive_ && !exceptionInstructionContext_;
        const std::uint16_t faultIr = returnActive_ ? returnIr_ : controlOp_ != ControlOp::None ? controlIr_
                                                                                                : state_.prefetch[0];
        const unsigned writes = exceptionLongFrame_ ? 7 : 3;
        const std::uint32_t savedPc = exceptionActive_ && exceptionStage_ >= writes
                                          ? exceptionVector_ * 4
                                          : state_.pc;
        // O PC empilhado segue o início da instrução nesta implementação.
        beginException(vector, savedPc, true);
        traceDeferred_ = false;
        exceptionIr_ = faultIr;
        exceptionAddress_ = address;
        // I/N distingue processamento de instrução e processamento de exceção.
        // R/W ocupa o bit 4; I/N, o bit 3; o código de função, os bits 2..0.
        exceptionStatus_ = (write ? 0 : 0x10) | (processingException ? 0x08 : 0) | static_cast<std::uint16_t>(functionCode);
    }

    void Moto68000::clockException()
    {
        if (exceptionIdle_)
        {
            --exceptionIdle_;
            bus_.observe(pins_);
            return;
        }
        const unsigned writes = exceptionLongFrame_ ? 7 : 3;
        if (interruptException_ && exceptionStage_ == 1 && !interruptAcknowledged_)
        {
            clockInterruptAcknowledge();
            return;
        }
        if (exceptionStage_ < writes)
        {
            // A pilha cresce para baixo; a última escrita deixa o SR no topo.
            const std::uint16_t longWords[] = {
                exceptionStatus_, static_cast<std::uint16_t>(exceptionAddress_),
                static_cast<std::uint16_t>(exceptionAddress_ >> 16), exceptionIr_,
                static_cast<std::uint16_t>(exceptionPc_),
                static_cast<std::uint16_t>(exceptionPc_ >> 16), exceptionSr_};
            const std::uint16_t shortWords[] = {
                static_cast<std::uint16_t>(exceptionPc_),
                static_cast<std::uint16_t>(exceptionPc_ >> 16), exceptionSr_};
            const auto result = transfer(state_.address[7] - 2, false, true,
                                         exceptionLongFrame_ ? longWords[exceptionStage_] : shortWords[exceptionStage_], 5);
            if (result == TransferResult::Complete)
            {
                state_.address[7] -= 2;
                ++exceptionStage_;
            }
            return;
        }
        const unsigned read = exceptionStage_ - writes;
        const std::uint32_t address = read < 2 ? exceptionVector_ * 4 + read * 2
                                               : exceptionTarget_ + (read - 2) * 2;
        const auto result = transfer(address, false, false, 0, read < 2 ? 5 : 6);
        if (result != TransferResult::Complete)
            return;
        if (read == 0)
            exceptionTarget_ = std::uint32_t(pendingWord_) << 16;
        else if (read == 1)
            exceptionTarget_ |= pendingWord_;
        else
            state_.prefetch[read - 2] = pendingWord_;
        if (++exceptionStage_ == writes + 4)
        {
            state_.pc = exceptionTarget_;
            exceptionActive_ = false;
            if (traceDeferred_)
            {
                tracePending_ = true;
                traceDeferred_ = false;
            }
        }
    }

    void Moto68000::clockInterruptAcknowledge()
    {
        pins_.active = true;
        pins_.interruptAcknowledge = true;
        pins_.interruptLevel = acknowledgedLevel_;
        pins_.functionCode = 7;
        pins_.address = 0x00fffff1 | (acknowledgedLevel_ << 1);
        pins_.phase = busPhase_;
        pins_.addressStrobe = busPhase_ >= 1;
        pins_.upperDataStrobe = busPhase_ >= 1;
        pins_.lowerDataStrobe = busPhase_ >= 1;
        if (busPhase_ == 2)
        {
            try
            {
                if (!bus_.tryAcknowledgeInterrupt(acknowledgedLevel_, interruptResponse_))
                {
                    pins_.waiting = true;
                    bus_.observe(pins_);
                    return;
                }
            }
            catch (const M68KBusError &)
            {
                interruptResponse_.kind = M68KInterruptResponse::Kind::Spurious;
            }
        }
        if (busPhase_ != 3)
        {
            ++busPhase_;
            bus_.observe(pins_);
            return;
        }
        if (interruptResponse_.kind == M68KInterruptResponse::Kind::DeviceVector)
        {
            exceptionVector_ = interruptResponse_.vector;
            pins_.data = interruptResponse_.vector;
            pins_.dataValid = true;
        }
        else if (interruptResponse_.kind == M68KInterruptResponse::Kind::Spurious)
            exceptionVector_ = 24;
        else
            exceptionVector_ = 24 + acknowledgedLevel_;
        bus_.observe(pins_);
        busPhase_ = 0;
        interruptAcknowledged_ = true;
    }

    void Moto68000::clockReturn()
    {
        const std::uint32_t address = returnStage_ < 3
                                          ? state_.address[7] + returnStage_ * 2
                                          : returnPc_ + (returnStage_ - 3) * 2;
        const unsigned functionCode = returnStage_ < 3 ? 5
                                                       : (state_.sr & 0x2000 ? 6 : 2);
        if (transfer(address, false, false, 0, functionCode) != TransferResult::Complete)
            return;
        switch (returnStage_)
        {
        case 0:
            returnSr_ = pendingWord_;
            break;
        case 1:
            returnPc_ = std::uint32_t(pendingWord_) << 16;
            break;
        case 2:
            returnPc_ |= pendingWord_;
            {
                const auto nextSsp = state_.address[7] + 6;
                if (!(returnSr_ & 0x2000))
                {
                    ssp_ = nextSsp;
                    state_.address[7] = usp_;
                }
                else
                    state_.address[7] = nextSsp;
                state_.sr = returnSr_;
                state_.pc = returnPc_;
            }
            break;
        default:
            state_.prefetch[returnStage_ - 3] = pendingWord_;
            break;
        }
        if (++returnStage_ == 5)
        {
            ++state_.instructions;
            returnActive_ = false;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        }
    }

    void Moto68000::clockControl()
    {
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        const unsigned dataCode = state_.sr & 0x2000 ? 5 : 1;
        if (controlExtensionPending_)
        {
            if (transfer(state_.pc + 4, false, false, 0, programCode) == TransferResult::Complete)
            {
                controlTarget_ |= pendingWord_;
                controlExtensionPending_ = false;
            }
            return;
        }
        if (controlIdle_)
        {
            --controlIdle_;
            bus_.observe(pins_);
            return;
        }

        const bool pushReturn = controlOp_ == ControlOp::Bsr || controlOp_ == ControlOp::Jsr;
        if (controlOp_ == ControlOp::Rts && controlStage_ < 2)
        {
            const auto result = transfer(state_.address[7] + controlStage_ * 2,
                                         false, false, 0, dataCode);
            if (result == TransferResult::Complete)
            {
                if (controlStage_ == 0)
                    controlTarget_ = std::uint32_t(pendingWord_) << 16;
                else
                    controlTarget_ |= pendingWord_;
                ++controlStage_;
            }
            return;
        }
        if (pushReturn && controlStage_ < 2)
        {
            const std::uint16_t word = controlStage_ == 0
                                           ? static_cast<std::uint16_t>(controlReturn_)
                                           : static_cast<std::uint16_t>(controlReturn_ >> 16);
            const auto result = transfer(state_.address[7] - 2, false, true, word, dataCode);
            if (result == TransferResult::Complete)
            {
                state_.address[7] -= 2;
                ++controlStage_;
            }
            return;
        }

        const unsigned read = controlStage_ - (controlOp_ == ControlOp::Jmp ? 0u : 2u);
        const auto result = transfer(controlTarget_ + read * 2, false, false, 0, programCode);
        if (result != TransferResult::Complete)
            return;
        state_.prefetch[read] = pendingWord_;
        ++controlStage_;
        if (read == 1)
        {
            if (controlOp_ == ControlOp::Rts)
                state_.address[7] += 4;
            state_.pc = controlTarget_;
            ++state_.instructions;
            controlOp_ = ControlOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        }
    }

    void Moto68000::clockExtendedMove()
    {
        auto &move = extendedMove_;
        const unsigned second = move.extensions == 2 ? 1 : 0;
        const unsigned reads = move.store || move.immediate ? 0 : (move.size == 4 ? 2 : 1);
        const unsigned writes = move.store ? (move.size == 4 ? 2 : 1) : 0;
        const unsigned readStart = second + 1;
        const unsigned followingStage = readStart + reads;
        const unsigned writeStart = followingStage + 1;
        const bool extensionRead = second && move.stage == 0;
        const bool nextRead = move.stage == second;
        const bool dataRead = move.stage >= readStart && move.stage < followingStage;
        const bool followingRead = move.stage == followingStage;
        const bool dataWrite = move.stage >= writeStart;
        const unsigned dataPart = dataRead ? move.stage - readStart : dataWrite ? move.stage - writeStart
                                                                                : 0;
        const std::uint32_t address = extensionRead ? state_.pc + 4 : nextRead    ? state_.pc + 2 + move.extensions * 2
                                                                  : followingRead ? state_.pc + 4 + move.extensions * 2
                                                                                  : move.address + (move.size == 4 ? dataPart * 2 : 0);
        const std::uint16_t word = move.size == 4 ? static_cast<std::uint16_t>(dataPart == 0 ? move.value >> 16 : move.value) : static_cast<std::uint16_t>(move.value);
        const bool supervisor = (state_.sr & 0x2000) != 0;
        if (transfer(address, move.size == 1 && (dataRead || dataWrite), dataWrite,
                     word, dataRead || dataWrite ? (supervisor ? 5 : 1) : (supervisor ? 6 : 2)) != TransferResult::Complete)
            return;

        if (extensionRead)
        {
            // O endereço absoluto longo e o imediato longo usam as duas extensões.
            if (move.mode == 7 && move.reg == 1)
                move.address = (std::uint32_t(move.firstExtension) << 16) | pendingWord_;
            if (move.immediate)
                move.value = (std::uint32_t(move.firstExtension) << 16) | pendingWord_;
        }
        else if (nextRead)
            move.nextOpcode = pendingWord_;
        else if (dataRead)
        {
            const std::uint32_t part = move.size == 1 ? pendingByte_ : pendingWord_;
            move.value = move.size == 4 && dataPart == 0 ? part << 16 : move.value | part;
        }
        else if (followingRead)
            move.nextFollowing = pendingWord_;

        if (++move.stage != writeStart + writes)
            return;

        const std::uint32_t mask = move.size == 1 ? 0xffu : move.size == 2 ? 0xffffu
                                                                           : 0xffffffffu;
        const std::uint32_t sign = move.size == 1 ? 0x80u : move.size == 2 ? 0x8000u
                                                                           : 0x80000000u;
        const std::uint32_t value = move.value & mask;
        if (!move.store)
            state_.data[move.dataReg] = (state_.data[move.dataReg] & ~mask) | value;
        state_.sr = (state_.sr & ~std::uint16_t(0x0f)) |
                    (value & sign ? 0x08 : 0) | (value == 0 ? 0x04 : 0);
        state_.prefetch = {move.nextOpcode, move.nextFollowing};
        state_.pc += 2 + move.extensions * 2;
        ++state_.instructions;
        memoryOp_ = MemoryOp::None;
        tracePending_ = traceInstruction_;
        traceInstruction_ = false;
    }

    void Moto68000::reset()
    {
        state_ = {};
        state_.sr = 0x2700;
        active_ = true;
        resetRead_ = 0;
        startupCycles_ = 0;
        busPhase_ = 0;
        pendingWord_ = 0;
        pins_ = {};
        branchActive_ = false;
        branchIdle_ = branchRead_ = 0;
        branchTarget_ = 0;
        branchReadsNeeded_ = branchIdleNeeded_ = 2;
        branchTaken_ = true;
        quickLongActive_ = false;
        quickIdle_ = 0;
        controlOp_ = ControlOp::None;
        controlStage_ = controlIdle_ = 0;
        controlExtensionPending_ = false;
        controlTarget_ = controlReturn_ = 0;
        controlIr_ = 0;
        memoryOp_ = MemoryOp::None;
        memoryStage_ = 0;
        memoryPrefetch_ = 0;
        memoryValue_ = 0;
        memorySize_ = 2;
        memoryMode_ = 2;
        memoryIdle_ = 0;
        memoryAddress_ = 0;
        memoryAddressReady_ = false;
        extendedMove_ = {};
        pendingByte_ = 0;
        halted_ = false;
        exceptionActive_ = false;
        exceptionLongFrame_ = false;
        exceptionInstructionContext_ = false;
        interruptException_ = interruptAcknowledged_ = false;
        acknowledgedLevel_ = 0;
        interruptResponse_ = {};
        exceptionIdle_ = exceptionStage_ = exceptionVector_ = 0;
        exceptionPc_ = exceptionAddress_ = exceptionTarget_ = 0;
        exceptionSr_ = exceptionIr_ = exceptionStatus_ = 0;
        returnActive_ = false;
        returnStage_ = 0;
        returnSr_ = 0;
        returnIr_ = 0;
        returnPc_ = 0;
        ssp_ = usp_ = 0;
        interruptLevel_ = 0;
        level7EdgePending_ = false;
        traceInstruction_ = tracePending_ = traceDeferred_ = false;
    }

    void Moto68000::clock()
    {
        ++state_.cycles;
        const auto instructionsBefore = state_.instructions;
        pins_ = {};
        if (halted_)
        {
            bus_.observe(pins_);
            return;
        }
        if (exceptionActive_)
        {
            clockException();
            return;
        }
        if (returnActive_)
        {
            clockReturn();
            return;
        }
        if (controlOp_ != ControlOp::None)
        {
            clockControl();
            return;
        }
        if (memoryOp_ == MemoryOp::ExtendedMove)
        {
            clockExtendedMove();
            return;
        }
        // Os 16 ciclos iniciais completam os 40 ciclos totais com seis leituras de quatro ciclos.
        if (!active_ || (!resetComplete() && startupCycles_ < 16))
        {
            if (active_)
                ++startupCycles_;
            bus_.observe(pins_);
            return;
        }

        if (resetComplete() && !branchActive_ && !quickLongActive_ &&
            memoryOp_ == MemoryOp::None && busPhase_ == 0)
        {
            if (tracePending_)
            {
                beginException(9, state_.pc, false);
                bus_.observe(pins_);
                return;
            }
            const unsigned mask = (state_.sr >> 8) & 7;
            if (level7EdgePending_ || interruptLevel_ > mask)
            {
                acknowledgedLevel_ = level7EdgePending_ ? 7 : interruptLevel_;
                if (acknowledgedLevel_ == 7)
                    level7EdgePending_ = false;
                beginException(0, state_.pc, false);
                interruptException_ = true;
                exceptionIdle_ = 11;
                state_.sr = (state_.sr & ~std::uint16_t(0x0700)) |
                            static_cast<std::uint16_t>(acknowledgedLevel_ << 8);
                bus_.observe(pins_);
                return;
            }
        }

        if (resetComplete() && !branchActive_ && !quickLongActive_ && memoryOp_ == MemoryOp::None && busPhase_ == 0)
        {
            traceInstruction_ = (state_.sr & 0x8000) != 0;
            const auto opcode = state_.prefetch[0];
            if (opcode == 0x4e73)
            {
                if (!(state_.sr & 0x2000))
                {
                    beginException(8, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                returnActive_ = true;
                returnStage_ = 0;
                returnPc_ = 0;
                returnIr_ = opcode;
                clockReturn();
                return;
            }
            if ((opcode & 0xfff0) == 0x4e40 ||
                opcode == 0x4afa || opcode == 0x4afb || opcode == 0x4afc ||
                (opcode & 0xf000) == 0xa000 || (opcode & 0xf000) == 0xf000)
            {
                const bool trap = (opcode & 0xfff0) == 0x4e40;
                const unsigned vector = trap ? 32 + (opcode & 15) : (opcode & 0xf000) == 0xa000 ? 10
                                                                : (opcode & 0xf000) == 0xf000   ? 11
                                                                                                : 4;
                if (trap)
                    ++state_.instructions;
                const bool tracedTrap = trap && traceInstruction_;
                beginException(vector, state_.pc + (trap ? 2 : 0), false, trap);
                traceDeferred_ = tracedTrap;
                bus_.observe(pins_);
                return;
            }
            if (opcode == 0x4e75)
            {
                controlOp_ = ControlOp::Rts;
                controlStage_ = controlIdle_ = 0;
                controlExtensionPending_ = false;
                controlTarget_ = 0;
                controlIr_ = opcode;
                clockControl();
                return;
            }
            if ((opcode & 0xffc0) == 0x4e80 || (opcode & 0xffc0) == 0x4ec0)
            {
                const unsigned mode = (opcode >> 3) & 7, reg = opcode & 7;
                if (mode != 2 && mode != 5 && !(mode == 7 && reg <= 2))
                    throw std::logic_error("Modo de endereçamento de JSR/JMP não implementado");
                controlOp_ = (opcode & 0x0040) ? ControlOp::Jmp : ControlOp::Jsr;
                controlStage_ = 0;
                controlIdle_ = mode == 2 || (mode == 7 && reg == 1) ? 0 : 2;
                controlExtensionPending_ = mode == 7 && reg == 1;
                controlIr_ = opcode;
                controlReturn_ = state_.pc + (mode == 2 ? 2 : mode == 7 && reg == 1 ? 6
                                                                                    : 4);
                if (mode == 2)
                    controlTarget_ = state_.address[reg];
                else if (mode == 5)
                    controlTarget_ = state_.address[reg] + signedWord(state_.prefetch[1]);
                else if (reg == 0)
                    controlTarget_ = static_cast<std::uint32_t>(signedWord(state_.prefetch[1]));
                else if (reg == 1)
                    controlTarget_ = std::uint32_t(state_.prefetch[1]) << 16;
                else
                    controlTarget_ = state_.pc + 2 + signedWord(state_.prefetch[1]);
                clockControl();
                return;
            }
            if ((opcode & 0xf000) == 0x6000)
            {
                const unsigned condition = (opcode >> 8) & 15;
                if (condition == 1)
                {
                    const bool wordDisplacement = (opcode & 0xff) == 0;
                    const int displacement = wordDisplacement  ? signedWord(state_.prefetch[1])
                                             : (opcode & 0x80) ? int(opcode & 0xff) - 0x100
                                                               : int(opcode & 0xff);
                    controlOp_ = ControlOp::Bsr;
                    controlStage_ = 0;
                    controlIdle_ = 2;
                    controlExtensionPending_ = false;
                    controlTarget_ = state_.pc + 2 + displacement;
                    controlReturn_ = state_.pc + (wordDisplacement ? 4 : 2);
                    controlIr_ = opcode;
                    clockControl();
                    return;
                }
                const bool wordDisplacement = (opcode & 0xff) == 0;
                int displacement = wordDisplacement
                                       ? int(state_.prefetch[1])
                                       : int(opcode & 0xff);
                if (displacement & (wordDisplacement ? 0x8000 : 0x80))
                    displacement -= wordDisplacement ? 0x10000 : 0x100;
                branchTaken_ = branchCondition(condition, state_.sr);
                branchTarget_ = branchTaken_ ? state_.pc + 2 + displacement
                                             : state_.pc + (wordDisplacement ? 4 : 2);
                branchReadsNeeded_ = branchTaken_ || wordDisplacement ? 2 : 1;
                branchIdleNeeded_ = branchTaken_ ? 2 : 4;
                branchActive_ = true;
                branchIdle_ = branchRead_ = 0;
            }
            else if ((opcode & 0xf000) == 0x5000 && (opcode & 0x0038) == 0 &&
                     ((opcode >> 6) & 3) != 3)
            {
                if (((opcode >> 6) & 3) == 2)
                {
                    quickLongActive_ = true;
                    quickIdle_ = 0;
                }
            }
            else if ((opcode & 0xf1ff) == 0x41f8)
            {
                memoryOp_ = MemoryOp::Lea;
                memoryStage_ = 0;
            }
            else if ((opcode >> 12) >= 1 && (opcode >> 12) <= 3 &&
                     (((((opcode >> 6) & 7) == 0) && (((opcode >> 3) & 7) >= 2) &&
                       (((opcode >> 3) & 7) <= 4)) ||
                      ((((opcode >> 3) & 7) == 0) && (((opcode >> 6) & 7) >= 2) &&
                       (((opcode >> 6) & 7) <= 4))))
            {
                memoryOp_ = ((opcode >> 6) & 7) == 0 ? MemoryOp::Load : MemoryOp::Store;
                memorySize_ = (opcode >> 12) == 1 ? 1 : (opcode >> 12) == 2 ? 4
                                                                            : 2;
                memoryMode_ = memoryOp_ == MemoryOp::Load ? (opcode >> 3) & 7 : (opcode >> 6) & 7;
                memoryIdle_ = memoryOp_ == MemoryOp::Load && memoryMode_ == 4 ? 2 : 0;
                memoryAddressReady_ = false;
                memoryStage_ = 0;
                memoryValue_ = 0;
            }
            else if (extendedMoveOpcode(opcode))
            {
                memoryOp_ = MemoryOp::ExtendedMove;
                auto &move = extendedMove_;
                move = {};
                move.size = (opcode >> 12) == 1 ? 1 : (opcode >> 12) == 2 ? 4
                                                                          : 2;
                move.store = ((opcode >> 3) & 7) == 0;
                move.mode = move.store ? (opcode >> 6) & 7 : (opcode >> 3) & 7;
                move.reg = move.store ? (opcode >> 9) & 7 : opcode & 7;
                move.dataReg = move.store ? opcode & 7 : (opcode >> 9) & 7;
                move.immediate = !move.store && move.mode == 7 && move.reg == 4;
                move.extensions = (move.mode == 7 && move.reg == 1) ||
                                          (move.immediate && move.size == 4)
                                      ? 2
                                      : 1;
                move.firstExtension = state_.prefetch[1];
                move.value = move.store ? state_.data[move.dataReg] : move.immediate ? move.firstExtension
                                                                                     : 0;
                if (move.mode == 5)
                    move.address = state_.address[move.reg] + signedWord(move.firstExtension);
                else if (move.mode == 7 && move.reg == 0)
                    move.address = static_cast<std::uint32_t>(signedWord(move.firstExtension));
                clockExtendedMove();
                return;
            }
            else if (opcode != 0x4e71 && (opcode & 0xf100) != 0x7000)
                throw std::logic_error("68000 opcode not implemented");
        }
        if (branchActive_ && branchIdle_ < branchIdleNeeded_)
        {
            ++branchIdle_;
            bus_.observe(pins_);
            return;
        }
        if (quickLongActive_ && quickIdle_ < 4)
        {
            ++quickIdle_;
            bus_.observe(pins_);
            return;
        }
        if (memoryIdle_)
        {
            --memoryIdle_;
            bus_.observe(pins_);
            return;
        }

        const auto opcode = state_.prefetch[0];
        const bool dataRead = memoryOp_ == MemoryOp::Load &&
                              memoryStage_ < (memorySize_ == 4 ? 2u : 1u);
        const bool dataWrite = memoryOp_ == MemoryOp::Store && memoryStage_ >= 1;
        const bool byteTransfer = memorySize_ == 1 && (dataRead || dataWrite);
        const unsigned addressReg = memoryOp_ == MemoryOp::Store ? (opcode >> 9) & 7 : opcode & 7;
        if ((dataRead || dataWrite) && !memoryAddressReady_)
        {
            const unsigned step = memorySize_ == 1 && addressReg == 7 ? 2 : memorySize_;
            memoryAddress_ = memoryMode_ == 4 ? state_.address[addressReg] - step : state_.address[addressReg];
            if (memoryMode_ == 4)
                state_.address[addressReg] = memoryAddress_;
            memoryAddressReady_ = true;
        }
        const unsigned dataPart = (dataRead || dataWrite) && memorySize_ == 4 ? (dataRead ? memoryStage_ : memoryStage_ - 1) : 0;
        const bool reverseLongStore = dataWrite && memorySize_ == 4 && memoryMode_ == 4;
        const std::uint32_t address = dataRead || dataWrite        ? memoryAddress_ + (reverseLongStore ? (1 - dataPart) * 2 : dataPart * 2)
                                      : !resetComplete()           ? (resetRead_ < 4 ? resetRead_ * 2 : state_.pc + (resetRead_ - 4) * 2)
                                      : branchActive_              ? (branchTaken_ ? branchTarget_ : state_.pc + 4) + branchRead_ * 2
                                      : memoryOp_ == MemoryOp::Lea ? state_.pc + 4 + memoryStage_ * 2
                                                                   : state_.pc + 4;
        const std::uint32_t source = state_.data[opcode & 7];
        const std::uint16_t writeValue = memorySize_ == 4 ? static_cast<std::uint16_t>((dataPart == 0) == !reverseLongStore ? source >> 16 : source) : static_cast<std::uint16_t>(source);
        // Buscas usam o espaço de programa supervisor; operandos, o espaço de dados.
        const bool supervisor = (state_.sr & 0x2000) != 0;
        if (transfer(address, byteTransfer, dataWrite, writeValue,
                     dataRead || dataWrite ? (supervisor ? 5 : 1) : (supervisor ? 6 : 2)) != TransferResult::Complete)
            return;

        if (resetRead_ < 4)
        {
            switch (resetRead_)
            {
            case 0:
                state_.address[7] = std::uint32_t(pendingWord_) << 16;
                break;
            case 1:
                state_.address[7] |= pendingWord_;
                ssp_ = state_.address[7];
                break;
            case 2:
                state_.pc = std::uint32_t(pendingWord_) << 16;
                break;
            case 3:
                state_.pc |= pendingWord_;
                break;
            }
        }
        else if (!resetComplete())
            state_.prefetch[resetRead_ - 4] = pendingWord_;
        else if (memoryOp_ != MemoryOp::None)
        {
            if (memoryOp_ == MemoryOp::Lea)
            {
                if (memoryStage_++ == 0)
                    memoryPrefetch_ = pendingWord_;
                else
                {
                    const unsigned reg = (opcode >> 9) & 7;
                    const int extension = state_.prefetch[1];
                    state_.address[reg] = static_cast<std::uint32_t>(extension & 0x8000 ? extension - 0x10000 : extension);
                    state_.prefetch = {memoryPrefetch_, pendingWord_};
                    state_.pc += 4;
                    ++state_.instructions;
                    memoryOp_ = MemoryOp::None;
                }
            }
            else if (memoryOp_ == MemoryOp::Load)
            {
                const unsigned dataReads = memorySize_ == 4 ? 2 : 1;
                if (memoryStage_ < dataReads)
                {
                    const std::uint32_t part = memorySize_ == 1 ? pendingByte_ : pendingWord_;
                    memoryValue_ = memorySize_ == 4 && memoryStage_ == 0
                                       ? part << 16
                                       : memoryValue_ | part;
                    ++memoryStage_;
                    if (memoryMode_ == 3 && memoryStage_ == dataReads)
                        state_.address[addressReg] += memorySize_ == 1 && addressReg == 7 ? 2 : memorySize_;
                }
                else
                {
                    const unsigned reg = (opcode >> 9) & 7;
                    const std::uint32_t mask = memorySize_ == 1 ? 0xffu : memorySize_ == 2 ? 0xffffu
                                                                                           : 0xffffffffu;
                    const std::uint32_t sign = memorySize_ == 1 ? 0x80u : memorySize_ == 2 ? 0x8000u
                                                                                           : 0x80000000u;
                    state_.data[reg] = (state_.data[reg] & ~mask) | memoryValue_;
                    state_.sr = (state_.sr & ~std::uint16_t(0x0f)) |
                                (memoryValue_ & sign ? 0x08 : 0) | (memoryValue_ == 0 ? 0x04 : 0);
                    state_.prefetch = {state_.prefetch[1], pendingWord_};
                    state_.pc += 2;
                    ++state_.instructions;
                    memoryOp_ = MemoryOp::None;
                }
            }
            else
            {
                if (memoryStage_ == 0)
                {
                    memoryPrefetch_ = pendingWord_;
                    ++memoryStage_;
                }
                else if (++memoryStage_ == (memorySize_ == 4 ? 3u : 2u))
                {
                    if (memoryMode_ == 3)
                        state_.address[addressReg] += memorySize_ == 1 && addressReg == 7 ? 2 : memorySize_;
                    const unsigned bits = memorySize_ * 8;
                    const std::uint32_t mask = memorySize_ == 4 ? 0xffffffffu : (1u << bits) - 1;
                    const std::uint32_t sign = 1u << (bits - 1);
                    const std::uint32_t value = source & mask;
                    state_.sr = (state_.sr & ~std::uint16_t(0x0f)) |
                                (value & sign ? 0x08 : 0) | (value == 0 ? 0x04 : 0);
                    state_.prefetch = {state_.prefetch[1], memoryPrefetch_};
                    state_.pc += 2;
                    ++state_.instructions;
                    memoryOp_ = MemoryOp::None;
                }
            }
        }
        else if (branchActive_)
        {
            if (branchTaken_ || branchReadsNeeded_ == 2)
                state_.prefetch[branchRead_] = pendingWord_;
            else
            {
                state_.prefetch[0] = state_.prefetch[1];
                state_.prefetch[1] = pendingWord_;
            }
            if (++branchRead_ == branchReadsNeeded_)
            {
                state_.pc = branchTarget_;
                ++state_.instructions;
                branchActive_ = false;
            }
        }
        else
        {
            const auto opcode = state_.prefetch[0];
            if ((opcode & 0xf100) == 0x7000)
            {
                int value = opcode & 0xff;
                if (value & 0x80)
                    value -= 0x100;
                state_.data[(opcode >> 9) & 7] = static_cast<std::uint32_t>(value);
                // MOVEQ preserva X e atualiza N/Z; V e C são limpos.
                state_.sr = (state_.sr & ~std::uint16_t(0x0f)) |
                            (value < 0 ? 0x08 : 0) | (value == 0 ? 0x04 : 0);
            }
            else if ((opcode & 0xf000) == 0x5000)
            {
                const unsigned size = (opcode >> 6) & 3;
                const std::uint32_t mask = size == 0 ? 0xffu : size == 1 ? 0xffffu
                                                                         : 0xffffffffu;
                const std::uint32_t sign = size == 0 ? 0x80u : size == 1 ? 0x8000u
                                                                         : 0x80000000u;
                const unsigned reg = opcode & 7;
                const unsigned encodedCount = (opcode >> 9) & 7;
                const unsigned count = encodedCount ? encodedCount : 8;
                const bool subtract = (opcode & 0x0100) != 0;
                const std::uint32_t original = state_.data[reg];
                const std::uint32_t left = original & mask;
                const std::uint32_t result = (subtract ? left - count : left + count) & mask;
                const bool carry = subtract ? left < count
                                            : std::uint64_t(left) + count > mask;
                const bool overflow = subtract
                                          ? ((left ^ count) & (left ^ result) & sign) != 0
                                          : ((~(left ^ count)) & (left ^ result) & sign) != 0;
                state_.data[reg] = (original & ~mask) | result;
                state_.sr = (state_.sr & ~std::uint16_t(0x1f)) |
                            (carry ? 0x11 : 0) | (result & sign ? 0x08 : 0) |
                            (result == 0 ? 0x04 : 0) | (overflow ? 0x02 : 0);
                quickLongActive_ = false;
            }
            state_.prefetch[0] = state_.prefetch[1];
            state_.prefetch[1] = pendingWord_;
            state_.pc += 2;
            ++state_.instructions;
        }
        if (!resetComplete())
            ++resetRead_;
        if (resetComplete() && state_.instructions != instructionsBefore)
        {
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        }
    }
}
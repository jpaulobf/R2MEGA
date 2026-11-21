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

        int signedByte(std::uint8_t value)
        {
            return value & 0x80 ? int(value) - 0x100 : int(value);
        }

        struct ShiftResult
        {
            std::uint32_t value = 0;
            std::uint16_t flags = 0;
        };

        ShiftResult shifted(std::uint32_t value, unsigned bits, unsigned kind,
                            bool left, unsigned count, std::uint16_t sr)
        {
            const std::uint32_t mask = bits == 32 ? 0xffffffffu : (1u << bits) - 1;
            const std::uint32_t sign = 1u << (bits - 1);
            value &= mask;
            bool extend = (sr & 0x10) != 0;
            bool carry = kind == 2 && count == 0 ? extend : false;
            bool overflow = false;
            for (unsigned i = 0; i < count; ++i)
            {
                const bool outgoing = left ? (value & sign) != 0 : (value & 1) != 0;
                const bool incoming = kind == 2 ? extend : kind == 3 ? outgoing
                                                                     : false;
                const std::uint32_t previous = value;
                if (left)
                    value = ((value << 1) | (incoming ? 1u : 0u)) & mask;
                else
                    value = (value >> 1) |
                            (kind == 0 ? previous & sign : incoming ? sign
                                                                    : 0u);
                if (kind == 0 && left && ((previous ^ value) & sign))
                    overflow = true;
                carry = outgoing;
                if (kind != 3)
                    extend = outgoing;
            }
            return {value, static_cast<std::uint16_t>(
                               (extend ? 0x10 : 0) | (value & sign ? 0x08 : 0) |
                               (value == 0 ? 0x04 : 0) | (overflow ? 0x02 : 0) |
                               (carry ? 0x01 : 0))};
        }

        bool branchCondition(unsigned condition, std::uint16_t sr)
        {
            const bool n = (sr & 8) != 0, z = (sr & 4) != 0;
            const bool v = (sr & 2) != 0, c = (sr & 1) != 0;
            switch (condition)
            {
            case 0:
                return true; // BRA
            case 1:
                return false; // DBF
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

        bool combinedMoveOpcode(std::uint16_t opcode)
        {
            if ((opcode >> 12) < 1 || (opcode >> 12) > 3)
                return false;
            const bool byte = (opcode >> 12) == 1;
            const unsigned sourceMode = (opcode >> 3) & 7, sourceReg = opcode & 7;
            const unsigned targetMode = (opcode >> 6) & 7, targetReg = (opcode >> 9) & 7;
            const bool registerSource = sourceMode == 0 ||
                                        (sourceMode == 1 && !byte);
            const bool memorySource = (sourceMode >= 2 && sourceMode <= 6) ||
                                      (sourceMode == 7 && sourceReg <= 3);
            const bool immediate = sourceMode == 7 && sourceReg == 4;
            const bool registerTarget = targetMode == 0 ||
                                        (targetMode == 1 && !byte);
            const bool memoryTarget = (targetMode >= 2 && targetMode <= 6) ||
                                      (targetMode == 7 && targetReg <= 1);
            return (registerSource || memorySource || immediate) &&
                   (registerTarget || memoryTarget);
        }

        unsigned moveExtensionWords(unsigned mode, unsigned reg, unsigned size)
        {
            if (mode == 5 || mode == 6)
                return 1;
            if (mode != 7)
                return 0;
            return reg == 1 || (reg == 4 && size == 4) ? 2 : 1;
        }

        std::uint32_t moveMemoryAddress(unsigned mode, unsigned reg,
                                        const std::array<std::uint32_t, 8> &addresses,
                                        const std::array<std::uint32_t, 8> &data,
                                        std::uint16_t first, std::uint16_t second,
                                        std::uint32_t pcBase)
        {
            if (mode >= 2 && mode <= 4)
                return addresses[reg];
            if (mode == 5)
                return addresses[reg] + signedWord(first);
            // No 68000, os bits de escala da extensão indexada são ignorados.
            if (mode == 6 || (mode == 7 && reg == 3))
            {
                const unsigned indexReg = (first >> 12) & 7;
                std::uint32_t index = first & 0x8000 ? addresses[indexReg] : data[indexReg];
                if (!(first & 0x0800))
                    index = static_cast<std::uint32_t>(
                        signedWord(static_cast<std::uint16_t>(index)));
                const std::uint32_t base = mode == 6 ? addresses[reg] : pcBase;
                return base + index + signedByte(static_cast<std::uint8_t>(first));
            }
            if (mode == 7 && reg == 0)
                return static_cast<std::uint32_t>(signedWord(first));
            if (mode == 7 && reg == 1)
                return (std::uint32_t(first) << 16) | second;
            // O PC da origem aponta para a primeira palavra de extensão.
            if (mode == 7 && reg == 2)
                return pcBase + signedWord(first);
            throw std::logic_error("Modo de endereçamento de MOVE não implementado");
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
        exceptionAccurateAddress_ = false;
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
        const bool accurateAddress = vector == 3 &&
                                     (memoryOp_ == MemoryOp::MultiplyDivide ||
                                      memoryOp_ == MemoryOp::Arithmetic ||
                                      memoryOp_ == MemoryOp::TestClearCompare ||
                                      memoryOp_ == MemoryOp::Movem ||
                                      (memoryOp_ == MemoryOp::ExtendedArithmetic &&
                                       extendedArithmetic_.compareMemory));
        const bool processingException = exceptionActive_ && !exceptionInstructionContext_;
        const std::uint16_t faultIr = returnActive_ ? returnIr_ : controlOp_ != ControlOp::None ? controlIr_
                                                                                                : state_.prefetch[0];
        const unsigned writes = exceptionLongFrame_ ? 7 : 3;
        const unsigned mode = memoryOp_ == MemoryOp::MultiplyDivide     ? multiplyDivide_.mode
                              : memoryOp_ == MemoryOp::Arithmetic       ? arithmetic_.mode
                              : memoryOp_ == MemoryOp::TestClearCompare ? testClearCompare_.mode
                                                                        : 0;
        const unsigned reg = memoryOp_ == MemoryOp::MultiplyDivide     ? multiplyDivide_.reg
                             : memoryOp_ == MemoryOp::Arithmetic       ? arithmetic_.reg
                             : memoryOp_ == MemoryOp::TestClearCompare ? testClearCompare_.reg
                                                                       : 0;
        const unsigned immediatePc = memoryOp_ == MemoryOp::Arithmetic && arithmetic_.immediateInstruction
                                         ? arithmetic_.immediateWords * 2
                                         : 0;
        const bool longSource = (memoryOp_ == MemoryOp::Arithmetic &&
                                 arithmetic_.size == 4 && !arithmetic_.memoryDestination) ||
                                (memoryOp_ == MemoryOp::TestClearCompare &&
                                 testClearCompare_.size == 4);
        const bool longPredecrement = (memoryOp_ == MemoryOp::Arithmetic && arithmetic_.size == 4) ||
                                      (memoryOp_ == MemoryOp::TestClearCompare && testClearCompare_.size == 4);
        std::uint32_t savedPc = state_.pc;
        if (exceptionActive_ && exceptionStage_ >= writes)
            savedPc = exceptionVector_ * 4;
        else if (accurateAddress)
        {
            if (memoryOp_ == MemoryOp::ExtendedArithmetic)
                savedPc += 4;
            else if (memoryOp_ == MemoryOp::Movem)
                savedPc += 6 + movem_.extensions * 2;
            else
            {
                const unsigned addressWords = mode == 4               ? (longPredecrement ? 2 : 4)
                                              : mode == 7 && reg == 0 ? 4
                                              : mode == 7 && reg == 1 ? 6
                                                                      : 2;
                savedPc += immediatePc + addressWords;
            }
        }
        // O PC salvo depende do estágio do cálculo do endereço efetivo.
        beginException(vector, savedPc, true);
        exceptionAccurateAddress_ = accurateAddress;
        if (accurateAddress)
            exceptionIdle_ += longSource ? 4 : 6;
        traceDeferred_ = false;
        exceptionIr_ = faultIr;
        exceptionAddress_ = address;
        // I/N distingue processamento de instrução e processamento de exceção.
        // R/W ocupa o bit 4; I/N, o bit 3; o código de função, os bits 2..0.
        exceptionStatus_ = (accurateAddress ? faultIr & 0xffe0 : 0) |
                           (write ? 0 : 0x10) |
                           (processingException ? 0x08 : 0) |
                           static_cast<std::uint16_t>(functionCode);
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
            // O quadro de endereço escreve palavras fora da ordem crescente da pilha.
            const std::uint16_t longWords[] = {
                static_cast<std::uint16_t>(exceptionPc_), exceptionSr_,
                static_cast<std::uint16_t>(exceptionPc_ >> 16), exceptionIr_,
                static_cast<std::uint16_t>(exceptionAddress_), exceptionStatus_,
                static_cast<std::uint16_t>(exceptionAddress_ >> 16)};
            const std::uint16_t legacyLongWords[] = {
                exceptionStatus_, static_cast<std::uint16_t>(exceptionAddress_),
                static_cast<std::uint16_t>(exceptionAddress_ >> 16), exceptionIr_,
                static_cast<std::uint16_t>(exceptionPc_),
                static_cast<std::uint16_t>(exceptionPc_ >> 16), exceptionSr_};
            const unsigned longOffsets[] = {2, 6, 4, 8, 10, 14, 12};
            const std::uint16_t shortWords[] = {
                static_cast<std::uint16_t>(exceptionPc_),
                static_cast<std::uint16_t>(exceptionPc_ >> 16), exceptionSr_};
            const auto stackAddress = exceptionAccurateAddress_
                                          ? state_.address[7] + exceptionStage_ * 2 - longOffsets[exceptionStage_]
                                          : state_.address[7] - 2;
            const auto result = transfer(stackAddress, false, true,
                                         exceptionLongFrame_ ? (exceptionAccurateAddress_ ? longWords[exceptionStage_]
                                                                                          : legacyLongWords[exceptionStage_])
                                                             : shortWords[exceptionStage_],
                                         5);
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
        if (exceptionAccurateAddress_ && read == 2)
            exceptionIdle_ = 2;
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
        const unsigned dataStart = second + 1;
        const unsigned followingStage = dataStart + reads + writes;
        const bool extensionRead = second && move.stage == 0;
        const bool nextRead = move.stage == second;
        const bool dataRead = reads && move.stage >= dataStart &&
                              move.stage < dataStart + reads;
        const bool followingRead = move.stage == followingStage;
        const bool dataWrite = writes && move.stage >= dataStart &&
                               move.stage < dataStart + writes;
        const unsigned dataPart = dataRead || dataWrite ? move.stage - dataStart : 0;
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
                move.address = moveMemoryAddress(move.mode, move.reg,
                                                 state_.address, state_.data, move.firstExtension, pendingWord_,
                                                 state_.pc + 2);
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

        if (++move.stage != followingStage + 1)
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

    void Moto68000::finishCombinedMove()
    {
        const auto &move = combinedMove_;
        const std::uint32_t mask = move.size == 1 ? 0xffu : move.size == 2 ? 0xffffu
                                                                           : 0xffffffffu;
        const std::uint32_t sign = move.size == 1 ? 0x80u : move.size == 2 ? 0x8000u
                                                                           : 0x80000000u;
        const std::uint32_t value = move.value & mask;
        if (move.targetMode == 0)
            state_.data[move.targetReg] =
                (state_.data[move.targetReg] & ~mask) | value;
        else if (move.targetMode == 1)
            state_.address[move.targetReg] = move.size == 2 ? static_cast<std::uint32_t>(signedWord(static_cast<std::uint16_t>(value))) : value;
        // MOVEA preserva todos os bits do SR; MOVE atualiza N/Z e limpa V/C.
        if (move.targetMode != 1)
            state_.sr = (state_.sr & ~std::uint16_t(0x0f)) |
                        (value & sign ? 0x08 : 0) | (value == 0 ? 0x04 : 0);
        state_.prefetch = {move.nextOpcode, move.nextFollowing};
        state_.pc += 2 + 2 * (move.sourceExtensions + move.targetExtensions);
        ++state_.instructions;
        memoryOp_ = MemoryOp::None;
        tracePending_ = traceInstruction_;
        traceInstruction_ = false;
    }

    void Moto68000::clockCombinedMove()
    {
        auto &move = combinedMove_;
        const bool supervisor = (state_.sr & 0x2000) != 0;
        const unsigned dataCode = supervisor ? 5 : 1;
        const unsigned programCode = supervisor ? 6 : 2;
        const unsigned words = move.size == 4 ? 2 : 1;
        const unsigned step = move.size == 1 && move.sourceReg == 7 ? 2 : move.size;

        // Cada leitura do programa desloca uma posição da fila de extensões e pré-busca.
        const auto fetchProgram = [&]() -> bool
        {
            if (transfer(state_.pc + move.streamIndex * 2, false, false, 0, programCode) != TransferResult::Complete)
                return false;
            const unsigned index = move.streamIndex++;
            if (index <= move.sourceExtensions)
                move.sourceExtension[index - 1] = pendingWord_;
            else if (index <= move.sourceExtensions + move.targetExtensions)
                move.targetExtension[index - move.sourceExtensions - 1] = pendingWord_;
            else if (index == move.sourceExtensions + move.targetExtensions + 1)
                move.nextOpcode = pendingWord_;
            else
                move.nextFollowing = pendingWord_;
            return true;
        };

        for (;;)
        {
            switch (move.phase)
            {
            case CombinedMove::Phase::SourceExtensions:
                if (move.sourceFetchesLeft)
                {
                    if (fetchProgram())
                        --move.sourceFetchesLeft;
                    return;
                }
                move.phase = CombinedMove::Phase::SourceIdle;
                continue;

            case CombinedMove::Phase::SourceIdle:
                if (move.sourceIdle)
                {
                    --move.sourceIdle;
                    bus_.observe(pins_);
                    return;
                }
                move.phase = CombinedMove::Phase::SourceRead;
                continue;

            case CombinedMove::Phase::SourceRead:
                if (move.immediate)
                {
                    move.value = move.size == 4 ? (std::uint32_t(move.sourceExtension[0]) << 16) | move.sourceExtension[1] : move.sourceExtension[0];
                    move.phase = CombinedMove::Phase::TargetExtensions;
                    continue;
                }
                if (move.sourceMode == 0 || move.sourceMode == 1)
                {
                    move.value = move.sourceMode == 0 ? state_.data[move.sourceReg] : state_.address[move.sourceReg];
                    move.phase = CombinedMove::Phase::TargetExtensions;
                    continue;
                }
                if (!move.sourceAddressReady)
                {
                    if (move.sourceMode == 4)
                        state_.address[move.sourceReg] -= step;
                    move.sourceAddress = moveMemoryAddress(move.sourceMode, move.sourceReg,
                                                           state_.address, state_.data, move.sourceExtension[0],
                                                           move.sourceExtension[1], state_.pc + 2);
                    move.sourceAddressReady = true;
                }
                // Fontes relativas ao PC leem o espaço de programa.
                if (transfer(move.sourceAddress + (move.size == 4 ? move.sourceReads * 2 : 0),
                             move.size == 1, false, 0,
                             move.sourceMode == 7 &&
                                     (move.sourceReg == 2 || move.sourceReg == 3)
                                 ? programCode
                                 : dataCode) == TransferResult::Complete)
                {
                    const std::uint32_t part = move.size == 1 ? pendingByte_ : pendingWord_;
                    move.value = move.size == 4 && move.sourceReads == 0 ? part << 16 : move.value | part;
                    if (++move.sourceReads == words)
                    {
                        if (move.sourceMode == 3)
                            state_.address[move.sourceReg] += step;
                        move.phase = CombinedMove::Phase::TargetExtensions;
                    }
                }
                return;

            case CombinedMove::Phase::TargetExtensions:
                if (move.targetFetchesLeft)
                {
                    if (fetchProgram())
                        --move.targetFetchesLeft;
                    return;
                }
                if (move.targetIdle)
                {
                    --move.targetIdle;
                    bus_.observe(pins_);
                    return;
                }
                move.phase = move.targetMode <= 1 ? CombinedMove::Phase::PrefetchAfterWrite : move.targetMode == 4 ? CombinedMove::Phase::PrefetchBeforeWrite
                                                                                                                   : CombinedMove::Phase::Write;
                continue;

            case CombinedMove::Phase::PrefetchBeforeWrite:
                if (fetchProgram())
                    move.phase = CombinedMove::Phase::Write;
                return;

            case CombinedMove::Phase::Write:
                if (!move.targetAddressReady)
                {
                    const unsigned targetStep = move.size == 1 && move.targetReg == 7 ? 2 : move.size;
                    if (move.targetMode == 4)
                        state_.address[move.targetReg] -= targetStep;
                    move.targetAddress = moveMemoryAddress(move.targetMode, move.targetReg,
                                                           state_.address, state_.data, move.targetExtension[0],
                                                           move.targetExtension[1], state_.pc + 2);
                    move.targetAddressReady = true;
                }
                {
                    const bool reverse = move.targetMode == 4 && move.size == 4;
                    const unsigned part = move.targetWrites;
                    const std::uint32_t address = move.targetAddress +
                                                  (move.size == 4 ? (reverse ? 1 - part : part) * 2 : 0);
                    const std::uint16_t value = move.size == 4 ? static_cast<std::uint16_t>((part == 0) == !reverse ? move.value >> 16 : move.value) : static_cast<std::uint16_t>(move.value);
                    if (transfer(address, move.size == 1, true, value, dataCode) == TransferResult::Complete)
                    {
                        if (++move.targetWrites == words)
                        {
                            if (move.targetMode == 3)
                                state_.address[move.targetReg] +=
                                    move.size == 1 && move.targetReg == 7 ? 2 : move.size;
                            if (move.targetMode == 4)
                                finishCombinedMove();
                            else
                                move.phase = CombinedMove::Phase::PrefetchAfterWrite;
                        }
                    }
                }
                return;

            case CombinedMove::Phase::PrefetchAfterWrite:
                if (fetchProgram() && move.streamIndex ==
                                          move.sourceExtensions + move.targetExtensions + 3)
                    finishCombinedMove();
                return;
            }
        }
    }

    void Moto68000::clockTestClearCompare()
    {
        auto &op = testClearCompare_;
        const bool supervisor = (state_.sr & 0x2000) != 0;
        const unsigned dataCode = supervisor ? 5 : 1;
        const unsigned programCode = supervisor ? 6 : 2;
        const unsigned words = op.size == 4 ? 2 : 1;
        const unsigned step = op.size == 1 && op.reg == 7 ? 2 : op.size;
        const bool clear = op.kind == TestClearCompare::Kind::Clear;

        const auto finish = [&]()
        {
            const std::uint32_t mask = op.size == 1 ? 0xffu : op.size == 2 ? 0xffffu
                                                                           : 0xffffffffu;
            const std::uint32_t sign = op.size == 1 ? 0x80u : op.size == 2 ? 0x8000u
                                                                           : 0x80000000u;
            if (clear && op.mode == 0)
                state_.data[op.reg] &= ~mask;

            if (clear)
                state_.sr = (state_.sr & ~std::uint16_t(0x0f)) | 0x04;
            else if (op.kind == TestClearCompare::Kind::Test)
            {
                const std::uint32_t value = op.value & mask;
                state_.sr = (state_.sr & ~std::uint16_t(0x0f)) |
                            (value & sign ? 0x08 : 0) | (value == 0 ? 0x04 : 0);
            }
            else
            {
                // CMPA.W estende o operando antes de subtrair os 32 bits de An.
                const bool addressCompare = op.kind == TestClearCompare::Kind::CompareAddress;
                const std::uint32_t source = addressCompare && op.size == 2 ? static_cast<std::uint32_t>(signedWord(static_cast<std::uint16_t>(op.value))) : op.value & mask;
                const std::uint32_t destination = addressCompare ? state_.address[op.destination] : state_.data[op.destination];
                const std::uint32_t arithmeticMask = addressCompare ? 0xffffffffu : mask;
                const std::uint32_t arithmeticSign = addressCompare ? 0x80000000u : sign;
                const std::uint32_t lhs = destination & arithmeticMask;
                const std::uint32_t rhs = source & arithmeticMask;
                const std::uint32_t result = (lhs - rhs) & arithmeticMask;
                state_.sr = (state_.sr & ~std::uint16_t(0x0f)) |
                            (result & arithmeticSign ? 0x08 : 0) |
                            (result == 0 ? 0x04 : 0) |
                            (((lhs ^ rhs) & (lhs ^ result) & arithmeticSign) ? 0x02 : 0) |
                            (lhs < rhs ? 0x01 : 0);
            }
            state_.prefetch = {op.nextOpcode, op.nextFollowing};
            state_.pc += 2 + op.extensions * 2;
            ++state_.instructions;
            memoryOp_ = MemoryOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        };

        for (;;)
        {
            switch (op.phase)
            {
            case TestClearCompare::Phase::Extensions:
                if (op.fetchesLeft)
                {
                    if (transfer(state_.pc + op.streamIndex * 2, false, false, 0,
                                 programCode) == TransferResult::Complete)
                    {
                        const unsigned index = op.streamIndex++;
                        if (index <= op.extensions)
                            op.extension[index - 1] = pendingWord_;
                        else
                            op.nextOpcode = pendingWord_;
                        --op.fetchesLeft;
                    }
                    return;
                }
                op.phase = TestClearCompare::Phase::Idle;
                continue;

            case TestClearCompare::Phase::Idle:
                if (op.idle)
                {
                    --op.idle;
                    bus_.observe(pins_);
                    return;
                }
                op.phase = TestClearCompare::Phase::Read;
                continue;

            case TestClearCompare::Phase::Read:
                if (op.mode == 0 || op.mode == 1 ||
                    (op.mode == 7 && op.reg == 4))
                {
                    if (op.mode == 0)
                        op.value = state_.data[op.reg];
                    else if (op.mode == 1)
                        op.value = state_.address[op.reg];
                    else
                        op.value = op.size == 4 ? (std::uint32_t(op.extension[0]) << 16) | op.extension[1] : op.extension[0];
                    op.phase = TestClearCompare::Phase::Prefetch;
                    continue;
                }
                if (!op.addressReady)
                {
                    if (op.mode == 4)
                        state_.address[op.reg] -= step;
                    op.address = moveMemoryAddress(op.mode, op.reg, state_.address,
                                                   state_.data, op.extension[0], op.extension[1], state_.pc + 2);
                    if (op.mode == 3 && op.size == 2)
                        state_.address[op.reg] += step;
                    op.addressReady = true;
                }
                if (transfer(op.address + (op.size == 4 ? op.reads * 2 : 0),
                             op.size == 1, false, 0,
                             op.mode == 7 && (op.reg == 2 || op.reg == 3) ? programCode : dataCode) == TransferResult::Complete)
                {
                    const std::uint32_t part = op.size == 1 ? pendingByte_ : pendingWord_;
                    op.value = op.size == 4 && op.reads == 0 ? part << 16 : op.value | part;
                    if (++op.reads == words)
                    {
                        if (op.mode == 3 && op.size != 2)
                            state_.address[op.reg] += step;
                        op.phase = TestClearCompare::Phase::Prefetch;
                    }
                }
                return;

            case TestClearCompare::Phase::Prefetch:
                if (transfer(state_.pc + (op.extensions + 2) * 2, false, false,
                             0, programCode) != TransferResult::Complete)
                    return;
                op.nextFollowing = pendingWord_;
                if (clear && op.mode != 0)
                    op.phase = TestClearCompare::Phase::Write;
                else
                    finish();
                return;

            case TestClearCompare::Phase::Write:
                if (transfer(op.address + (op.size == 4 ? op.writes * 2 : 0),
                             op.size == 1, true, 0, dataCode) == TransferResult::Complete &&
                    ++op.writes == words)
                    finish();
                return;
            }
        }
    }

    void Moto68000::clockArithmetic()
    {
        auto &op = arithmetic_;
        const bool supervisor = (state_.sr & 0x2000) != 0;
        const unsigned dataCode = supervisor ? 5 : 1;
        const unsigned programCode = supervisor ? 6 : 2;
        const unsigned words = op.size == 4 ? 2 : 1;
        const unsigned step = op.size == 1 && op.reg == 7 ? 2 : op.size;

        const auto calculate = [&]()
        {
            const std::uint32_t mask = op.addressDestination || op.size == 4 ? 0xffffffffu : op.size == 2 ? 0xffffu
                                                                                                          : 0xffu;
            const std::uint32_t sign = op.addressDestination || op.size == 4 ? 0x80000000u : op.size == 2 ? 0x8000u
                                                                                                          : 0x80u;
            if (op.unary != Arithmetic::Unary::None)
            {
                const std::uint32_t value = op.operand & mask;
                const bool extend = (state_.sr & 0x10) != 0;
                const bool complement = op.unary == Arithmetic::Unary::Complement;
                const bool withExtend = op.unary == Arithmetic::Unary::NegateExtend;
                op.result = complement ? (~value & mask) : (0u - value - (withExtend && extend ? 1u : 0u)) & mask;
                const bool carry = !complement && (value != 0 || (withExtend && extend));
                const bool overflow = !complement && value == sign &&
                                      !(withExtend && extend);
                const bool zero = withExtend ? (state_.sr & 0x04) && op.result == 0 : op.result == 0;
                op.flags = (complement ? state_.sr & 0x10 : carry ? 0x10
                                                                  : 0) |
                           (op.result & sign ? 0x08 : 0) | (zero ? 0x04 : 0) |
                           (overflow ? 0x02 : 0) | (carry ? 0x01 : 0);
                return;
            }
            const std::uint32_t source = op.quickInstruction ? op.quickCount : op.immediateInstruction             ? (op.size == 4 ? (std::uint32_t(op.extension[0]) << 16) | op.extension[1] : op.extension[0])
                                                                           : op.memoryDestination                  ? state_.data[op.dataReg]
                                                                           : op.addressDestination && op.size == 2 ? static_cast<std::uint32_t>(signedWord(static_cast<std::uint16_t>(op.operand)))
                                                                                                                   : op.operand;
            const std::uint32_t destination = op.memoryDestination ? op.operand : op.addressDestination ? state_.address[op.dataReg]
                                                                                                        : state_.data[op.dataReg];
            const std::uint32_t lhs = destination & mask, rhs = source & mask;
            if (op.logic != Arithmetic::Logic::None)
            {
                op.result = op.logic == Arithmetic::Logic::And ? lhs & rhs : op.logic == Arithmetic::Logic::Or ? lhs | rhs
                                                                                                               : lhs ^ rhs;
                op.flags = (state_.sr & 0x10) | (op.result & sign ? 0x08 : 0) |
                           (op.result == 0 ? 0x04 : 0);
                return;
            }
            op.result = op.subtract ? (lhs - rhs) & mask : (lhs + rhs) & mask;
            if (op.addressDestination)
                return;
            const bool carry = op.subtract ? lhs < rhs : std::uint64_t(lhs) + rhs > mask;
            const bool overflow = op.subtract ? ((lhs ^ rhs) & (lhs ^ op.result) & sign) != 0 : ((~(lhs ^ rhs)) & (lhs ^ op.result) & sign) != 0;
            op.flags = (carry ? 0x11 : 0) | (op.result & sign ? 0x08 : 0) |
                       (op.result == 0 ? 0x04 : 0) | (overflow ? 0x02 : 0);
        };

        const auto finish = [&]()
        {
            if (op.compareOnly)
                state_.sr = (state_.sr & ~std::uint16_t(0x0f)) | (op.flags & 0x0f);
            else if (op.addressDestination)
                state_.address[op.dataReg] = op.result;
            else
            {
                if (!op.memoryDestination || op.mode == 0)
                {
                    const std::uint32_t mask = op.size == 1 ? 0xffu : op.size == 2 ? 0xffffu
                                                                                   : 0xffffffffu;
                    const unsigned target = op.memoryDestination ? op.reg : op.dataReg;
                    state_.data[target] = (state_.data[target] & ~mask) | op.result;
                }
                state_.sr = (state_.sr & ~std::uint16_t(0x1f)) | op.flags;
            }
            state_.prefetch = {op.nextOpcode, op.nextFollowing};
            state_.pc += 2 + op.extensions * 2;
            ++state_.instructions;
            memoryOp_ = MemoryOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        };

        for (;;)
        {
            switch (op.phase)
            {
            case Arithmetic::Phase::Extensions:
                if (op.fetchesLeft)
                {
                    if (transfer(state_.pc + op.streamIndex * 2, false, false, 0,
                                 programCode) == TransferResult::Complete)
                    {
                        const unsigned index = op.streamIndex++;
                        if (index <= op.extensions)
                            op.extension[index - 1] = pendingWord_;
                        else
                            op.nextOpcode = pendingWord_;
                        --op.fetchesLeft;
                    }
                    return;
                }
                op.phase = Arithmetic::Phase::Idle;
                continue;

            case Arithmetic::Phase::Idle:
                if (op.idle)
                {
                    --op.idle;
                    bus_.observe(pins_);
                    return;
                }
                op.phase = Arithmetic::Phase::Read;
                continue;

            case Arithmetic::Phase::Read:
                if ((!op.memoryDestination &&
                     (op.mode <= 1 || (op.mode == 7 && op.reg == 4))) ||
                    (op.memoryDestination && op.mode == 0))
                {
                    op.operand = op.mode == 0 ? state_.data[op.reg] : op.mode == 1 ? state_.address[op.reg]
                                                                  : op.size == 4   ? (std::uint32_t(op.extension[0]) << 16) | op.extension[1]
                                                                                   : op.extension[0];
                    calculate();
                    op.phase = Arithmetic::Phase::Prefetch;
                    continue;
                }
                if (!op.addressReady)
                {
                    if (op.mode == 4)
                        state_.address[op.reg] -= step;
                    op.address = moveMemoryAddress(op.mode, op.reg, state_.address,
                                                   state_.data, op.extension[op.immediateWords],
                                                   op.extension[op.immediateWords + 1], state_.pc + 2);
                    if (op.mode == 3 && op.size == 2)
                        state_.address[op.reg] += step;
                    op.addressReady = true;
                }
                if (transfer(op.address + (op.size == 4 ? op.reads * 2 : 0),
                             op.size == 1, false, 0,
                             !op.memoryDestination && op.mode == 7 &&
                                     (op.reg == 2 || op.reg == 3)
                                 ? programCode
                                 : dataCode) == TransferResult::Complete)
                {
                    const std::uint32_t part = op.size == 1 ? pendingByte_ : pendingWord_;
                    op.operand = op.size == 4 && op.reads == 0 ? part << 16 : op.operand | part;
                    if (++op.reads == words)
                    {
                        if (op.mode == 3 && op.size != 2)
                            state_.address[op.reg] += step;
                        calculate();
                        op.phase = Arithmetic::Phase::Prefetch;
                    }
                }
                return;

            case Arithmetic::Phase::Prefetch:
                if (transfer(state_.pc + (op.extensions + 2) * 2, false, false,
                             0, programCode) != TransferResult::Complete)
                    return;
                op.nextFollowing = pendingWord_;
                if (op.memoryDestination && op.mode != 0 && !op.compareOnly)
                    op.phase = Arithmetic::Phase::Write;
                else
                    finish();
                return;

            case Arithmetic::Phase::Write:
            {
                const unsigned word = op.size == 4 ? 1 - op.writes : 0;
                const std::uint16_t value = op.size == 4
                                                ? static_cast<std::uint16_t>(word == 0 ? op.result >> 16 : op.result)
                                                : static_cast<std::uint16_t>(op.result);
                if (transfer(op.address + word * 2,
                             op.size == 1, true, value, dataCode) == TransferResult::Complete &&
                    ++op.writes == words)
                    finish();
                return;
            }
            }
        }
    }

    void Moto68000::clockMultiplyDivide()
    {
        auto &op = multiplyDivide_;
        const unsigned dataCode = state_.sr & 0x2000 ? 5 : 1;
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;

        const auto calculate = [&]()
        {
            const std::uint32_t dividend = state_.data[op.destination];
            if (!op.divide)
            {
                if (op.signedOperation)
                    op.result = static_cast<std::uint32_t>(
                        signedWord(op.source) * signedWord(static_cast<std::uint16_t>(dividend)));
                else
                    op.result = std::uint32_t(op.source) * (dividend & 0xffffu);
                // MULS conta as 16 transições até o zero acrescentado no bit menos significativo.
                std::uint32_t bits = op.signedOperation
                                         ? (std::uint32_t(op.source) ^ (std::uint32_t(op.source) << 1)) & 0xffffu
                                         : op.source;
                unsigned additions = 0;
                while (bits)
                {
                    additions += bits & 1u;
                    bits >>= 1;
                }
                // 38 + 2n ciclos, incluindo a pré-busca final de quatro ciclos.
                op.idle += 34 + 2 * additions;
                return;
            }

            if (op.source == 0)
            {
                ++state_.instructions;
                const bool traced = traceInstruction_;
                beginException(5, state_.pc + 2 + op.extensions * 2, false, true);
                traceDeferred_ = traced;
                return;
            }

            if (!op.signedOperation)
            {
                const std::uint32_t divisor = op.source;
                op.overflow = dividend >> 16 >= divisor;
                if (op.overflow)
                    op.idle += 6; // Estouro detectado antes das iterações da divisão.
                else
                {
                    const std::uint32_t quotient = dividend / divisor;
                    op.result = ((dividend % divisor) << 16) | quotient;
                    unsigned microcycles = 38;
                    std::uint32_t partial = dividend;
                    const std::uint32_t shiftedDivisor = divisor << 16;
                    for (unsigned i = 0; i < 15; ++i)
                    {
                        const std::uint32_t previous = partial;
                        partial <<= 1;
                        if (previous & 0x80000000u)
                            partial -= shiftedDivisor;
                        else
                        {
                            microcycles += 2;
                            if (partial >= shiftedDivisor)
                            {
                                partial -= shiftedDivisor;
                                --microcycles;
                            }
                        }
                    }
                    op.idle += microcycles * 2 - 4;
                }
                return;
            }

            const std::int64_t signedDividend = static_cast<std::int32_t>(dividend);
            const std::int64_t signedDivisor = signedWord(op.source);
            const std::int64_t quotient = signedDividend / signedDivisor;
            const std::int64_t remainder = signedDividend % signedDivisor;
            op.overflow = quotient < -32768 || quotient > 32767;
            if (!op.overflow)
                op.result = (std::uint32_t(static_cast<std::uint16_t>(remainder)) << 16) |
                            static_cast<std::uint16_t>(quotient);

            const std::uint64_t absoluteDividend = signedDividend < 0 ? -signedDividend : signedDividend;
            const std::uint64_t absoluteDivisor = signedDivisor < 0 ? -signedDivisor : signedDivisor;
            unsigned microcycles = 6 + (signedDividend < 0 ? 1 : 0);
            if ((absoluteDividend >> 16) >= absoluteDivisor)
                microcycles += 2;
            else
            {
                microcycles += 55;
                if (signedDivisor >= 0)
                {
                    if (signedDividend < 0)
                        ++microcycles;
                    else
                        --microcycles;
                }
                std::uint16_t absoluteQuotient = static_cast<std::uint16_t>(absoluteDividend / absoluteDivisor);
                for (unsigned i = 0; i < 15; ++i)
                {
                    if (!(absoluteQuotient & 0x8000u))
                        ++microcycles;
                    absoluteQuotient <<= 1;
                }
            }
            op.idle += microcycles * 2 - 4;
        };

        const auto finish = [&]()
        {
            if (!op.overflow)
            {
                state_.data[op.destination] = op.result;
                const std::uint32_t flagsValue = op.divide ? op.result & 0xffffu : op.result;
                const bool negative = op.divide ? (flagsValue & 0x8000u) != 0
                                                : (flagsValue & 0x80000000u) != 0;
                state_.sr = (state_.sr & ~std::uint16_t(0x1f)) |
                            (state_.sr & 0x10) | (negative ? 0x08 : 0) |
                            (flagsValue == 0 ? 0x04 : 0);
            }
            else
                // No 68000, o estouro observado deixa N e V ativos e limpa Z e C.
                state_.sr = (state_.sr & ~std::uint16_t(0x1f)) |
                            (state_.sr & 0x10) | 0x0a;
            state_.prefetch = {op.nextOpcode, op.nextFollowing};
            state_.pc += 2 + op.extensions * 2;
            ++state_.instructions;
            memoryOp_ = MemoryOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        };

        for (;;)
        {
            switch (op.phase)
            {
            case MultiplyDivide::Phase::Extensions:
                if (op.fetchesLeft)
                {
                    if (transfer(state_.pc + op.streamIndex * 2, false, false, 0,
                                 programCode) == TransferResult::Complete)
                    {
                        const unsigned index = op.streamIndex++;
                        if (index <= op.extensions)
                            op.extension[index - 1] = pendingWord_;
                        else
                            op.nextOpcode = pendingWord_;
                        --op.fetchesLeft;
                    }
                    return;
                }
                op.phase = MultiplyDivide::Phase::AddressIdle;
                continue;
            case MultiplyDivide::Phase::AddressIdle:
                if (op.idle)
                {
                    --op.idle;
                    bus_.observe(pins_);
                    return;
                }
                op.phase = MultiplyDivide::Phase::Read;
                continue;
            case MultiplyDivide::Phase::Read:
            {
                const bool memorySource = op.mode != 0 && !(op.mode == 7 && op.reg == 4);
                if (op.mode == 0 || (op.mode == 7 && op.reg == 4))
                {
                    op.source = op.mode == 0 ? static_cast<std::uint16_t>(state_.data[op.reg])
                                             : op.extension[0];
                }
                else
                {
                    if (!op.addressReady)
                    {
                        if (op.mode == 4)
                            state_.address[op.reg] -= 2;
                        op.address = moveMemoryAddress(op.mode, op.reg, state_.address,
                                                       state_.data, op.extension[0],
                                                       op.extension[1], state_.pc + 2);
                        if (op.mode == 3)
                            state_.address[op.reg] += 2;
                        op.addressReady = true;
                    }
                    if (transfer(op.address, false, false, 0,
                                 op.mode == 7 && (op.reg == 2 || op.reg == 3)
                                     ? programCode
                                     : dataCode) != TransferResult::Complete)
                        return;
                    op.source = pendingWord_;
                }
                calculate();
                if (memoryOp_ == MemoryOp::None)
                {
                    if (!memorySource)
                        bus_.observe(pins_);
                    return;
                }
                op.phase = op.divide ? MultiplyDivide::Phase::Internal
                                     : MultiplyDivide::Phase::Prefetch;
                if (memorySource)
                    return;
                continue;
            }
            case MultiplyDivide::Phase::Internal:
                if (op.idle)
                {
                    --op.idle;
                    if (!op.divide && op.idle == 0)
                        finish();
                    bus_.observe(pins_);
                    return;
                }
                if (op.divide)
                {
                    op.phase = MultiplyDivide::Phase::Prefetch;
                    continue;
                }
                finish();
                bus_.observe(pins_);
                return;
            case MultiplyDivide::Phase::Prefetch:
                if (transfer(state_.pc + (op.extensions + 2) * 2, false,
                             false, 0, programCode) != TransferResult::Complete)
                    return;
                op.nextFollowing = pendingWord_;
                if (op.divide)
                    finish();
                else
                    op.phase = MultiplyDivide::Phase::Internal;
                return;
            }
        }
    }

    void Moto68000::clockExtendedArithmetic()
    {
        auto &op = extendedArithmetic_;
        const unsigned dataCode = state_.sr & 0x2000 ? 5 : 1;
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        const unsigned words = op.size == 4 ? 2 : 1;

        const auto calculate = [&]()
        {
            const std::uint32_t mask = op.size == 1 ? 0xffu : op.size == 2 ? 0xffffu
                                                                           : 0xffffffffu;
            const std::uint32_t sign = op.size == 1 ? 0x80u : op.size == 2 ? 0x8000u
                                                                           : 0x80000000u;
            const std::uint32_t lhs = op.target & mask, rhs = op.source & mask;
            const std::uint32_t extend = !op.compareMemory && (state_.sr & 0x10) ? 1u : 0u;
            op.result = op.subtract ? (lhs - rhs - extend) & mask : (lhs + rhs + extend) & mask;
            const bool carry = op.subtract ? std::uint64_t(lhs) < std::uint64_t(rhs) + extend : std::uint64_t(lhs) + rhs + extend > mask;
            const bool overflow = op.subtract ? ((lhs ^ rhs) & (lhs ^ op.result) & sign) != 0 : ((~(lhs ^ rhs)) & (lhs ^ op.result) & sign) != 0;
            // ADDX/SUBX acumulam Z; CMPM calcula Z para cada comparação.
            const bool zero = op.compareMemory ? op.result == 0 : (state_.sr & 0x04) && op.result == 0;
            const std::uint16_t extendFlag = op.compareMemory ? state_.sr & 0x10 : carry ? 0x10
                                                                                         : 0;
            op.flags = extendFlag | (carry ? 0x01 : 0) |
                       (op.result & sign ? 0x08 : 0) |
                       (zero ? 0x04 : 0) |
                       (overflow ? 0x02 : 0);
        };

        const auto finish = [&]()
        {
            if (!op.memory)
            {
                const std::uint32_t mask = op.size == 1 ? 0xffu : op.size == 2 ? 0xffffu
                                                                               : 0xffffffffu;
                state_.data[op.targetReg] =
                    (state_.data[op.targetReg] & ~mask) | op.result;
            }
            state_.sr = (state_.sr & ~std::uint16_t(0x1f)) | op.flags;
            state_.prefetch = {op.nextOpcode, op.nextFollowing};
            state_.pc += 2;
            ++state_.instructions;
            memoryOp_ = MemoryOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        };

        for (;;)
        {
            switch (op.phase)
            {
            case ExtendedArithmetic::Phase::Idle:
                if (op.idle)
                {
                    --op.idle;
                    bus_.observe(pins_);
                    return;
                }
                op.phase = ExtendedArithmetic::Phase::SourceRead;
                continue;

            case ExtendedArithmetic::Phase::SourceRead:
                if (!op.memory)
                {
                    op.source = state_.data[op.sourceReg];
                    op.target = state_.data[op.targetReg];
                    calculate();
                    op.phase = ExtendedArithmetic::Phase::Prefetch;
                    continue;
                }
                if (!op.sourceAddressReady)
                {
                    if (!op.compareMemory)
                        state_.address[op.sourceReg] -=
                            op.size == 1 && op.sourceReg == 7 ? 2 : op.size;
                    op.sourceAddress = state_.address[op.sourceReg];
                    if (op.compareMemory)
                        state_.address[op.sourceReg] +=
                            op.size == 1 && op.sourceReg == 7 ? 2 : op.size == 4 ? 2
                                                                                 : op.size;
                    op.sourceAddressReady = true;
                }
                if (transfer(op.sourceAddress + (op.size == 4 ? op.sourceReads * 2 : 0),
                             op.size == 1, false, 0, dataCode) == TransferResult::Complete)
                {
                    const std::uint32_t part = op.size == 1 ? pendingByte_ : pendingWord_;
                    op.source = op.size == 4 && op.sourceReads == 0 ? part << 16 : op.source | part;
                    if (op.compareMemory && op.size == 4 && op.sourceReads == 0)
                        state_.address[op.sourceReg] += 2;
                    if (++op.sourceReads == words)
                    {
                        op.phase = ExtendedArithmetic::Phase::TargetRead;
                    }
                }
                return;

            case ExtendedArithmetic::Phase::TargetRead:
                if (!op.targetAddressReady)
                {
                    if (!op.compareMemory)
                        state_.address[op.targetReg] -=
                            op.size == 1 && op.targetReg == 7 ? 2 : op.size;
                    op.targetAddress = state_.address[op.targetReg];
                    op.targetAddressReady = true;
                }
                if (transfer(op.targetAddress + (op.size == 4 ? op.targetReads * 2 : 0),
                             op.size == 1, false, 0, dataCode) == TransferResult::Complete)
                {
                    const std::uint32_t part = op.size == 1 ? pendingByte_ : pendingWord_;
                    op.target = op.size == 4 && op.targetReads == 0 ? part << 16 : op.target | part;
                    if (++op.targetReads == words)
                    {
                        if (op.compareMemory)
                            state_.address[op.targetReg] +=
                                op.size == 1 && op.targetReg == 7 ? 2 : op.size;
                        calculate();
                        op.phase = ExtendedArithmetic::Phase::Prefetch;
                    }
                }
                return;

            case ExtendedArithmetic::Phase::Prefetch:
                if (transfer(state_.pc + 4, false, false, 0, programCode) != TransferResult::Complete)
                    return;
                op.nextFollowing = pendingWord_;
                if (op.memory && !op.compareMemory)
                    op.phase = ExtendedArithmetic::Phase::Write;
                else
                    finish();
                return;

            case ExtendedArithmetic::Phase::Write:
            {
                const std::uint16_t value = op.size == 4 ? static_cast<std::uint16_t>(op.writes == 0 ? op.result >> 16 : op.result) : static_cast<std::uint16_t>(op.result);
                if (transfer(op.targetAddress + (op.size == 4 ? op.writes * 2 : 0),
                             op.size == 1, true, value, dataCode) == TransferResult::Complete &&
                    ++op.writes == words)
                    finish();
                return;
            }
            }
        }
    }

    void Moto68000::clockStatusImmediate()
    {
        auto &op = statusImmediate_;
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        switch (op.phase)
        {
        case StatusImmediate::Phase::FirstFetch:
            if (transfer(state_.pc + 4, false, false, 0, programCode) == TransferResult::Complete)
                op.phase = StatusImmediate::Phase::Idle;
            return;

        case StatusImmediate::Phase::Idle:
            --op.idle;
            if (op.idle == 0)
            {
                const bool fullStatus = (op.opcode & 0x0040) != 0;
                const std::uint16_t mask = fullStatus ? 0xffff : 0x001f;
                const std::uint16_t source = op.immediate & mask;
                const std::uint16_t previous = state_.sr;
                const std::uint16_t result = (op.opcode & 0xff00) == 0x0000 ? previous | source : (op.opcode & 0xff00) == 0x0200 ? previous & source
                                                                                                                                 : previous ^ source;
                const std::uint16_t next = (previous & ~mask) | (result & mask);
                if (fullStatus && (previous & 0x2000) && !(next & 0x2000))
                {
                    ssp_ = state_.address[7];
                    state_.address[7] = usp_;
                }
                state_.sr = next;
                op.phase = StatusImmediate::Phase::OpcodeFetch;
            }
            bus_.observe(pins_);
            return;

        case StatusImmediate::Phase::OpcodeFetch:
            if (transfer(state_.pc + 4, false, false, 0, programCode) == TransferResult::Complete)
            {
                op.nextOpcode = pendingWord_;
                op.phase = StatusImmediate::Phase::FollowingFetch;
            }
            return;

        case StatusImmediate::Phase::FollowingFetch:
            if (transfer(state_.pc + 6, false, false, 0, programCode) == TransferResult::Complete)
            {
                op.nextFollowing = pendingWord_;
                state_.prefetch = {op.nextOpcode, op.nextFollowing};
                state_.pc += 4;
                ++state_.instructions;
                memoryOp_ = MemoryOp::None;
                tracePending_ = traceInstruction_;
                traceInstruction_ = false;
            }
            return;
        }
    }

    void Moto68000::clockBitOperation()
    {
        auto &op = bitOperation_;
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        const unsigned dataCode = state_.sr & 0x2000 ? 5 : 1;
        const unsigned step = op.reg == 7 ? 2 : 1;
        const unsigned firstEaExtension = op.staticBit ? 1 : 0;

        const auto calculate = [&]()
        {
            op.bit = (op.staticBit ? op.extension[0] : state_.data[op.bitReg]) &
                     (op.mode == 0 ? 31u : 7u);
            const std::uint32_t bitMask = 1u << op.bit;
            op.originalZero = (op.value & bitMask) == 0;
            op.result = op.kind == 1 ? op.value ^ bitMask : op.kind == 2 ? op.value & ~bitMask
                                                        : op.kind == 3   ? op.value | bitMask
                                                                         : op.value;
        };
        const auto finish = [&]()
        {
            if (op.kind != 0 && op.mode == 0)
                state_.data[op.reg] = op.result;
            state_.sr = (state_.sr & ~std::uint16_t(0x04)) |
                        (op.originalZero ? 0x04 : 0);
            state_.prefetch = {op.nextOpcode, op.nextFollowing};
            state_.pc += 2 + op.extensions * 2;
            ++state_.instructions;
            memoryOp_ = MemoryOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        };

        for (;;)
        {
            switch (op.phase)
            {
            case BitOperation::Phase::Extensions:
                if (op.fetchesLeft)
                {
                    if (transfer(state_.pc + op.streamIndex * 2, false, false,
                                 0, programCode) == TransferResult::Complete)
                    {
                        const unsigned index = op.streamIndex++;
                        if (index <= op.extensions)
                            op.extension[index - 1] = pendingWord_;
                        else
                            op.nextOpcode = pendingWord_;
                        --op.fetchesLeft;
                    }
                    return;
                }
                op.phase = BitOperation::Phase::Idle;
                continue;

            case BitOperation::Phase::Idle:
                if (op.idle)
                {
                    --op.idle;
                    bus_.observe(pins_);
                    return;
                }
                op.phase = BitOperation::Phase::Read;
                continue;

            case BitOperation::Phase::Read:
                if (op.mode == 0 || (op.mode == 7 && op.reg == 4))
                {
                    op.value = op.mode == 0 ? state_.data[op.reg] : op.extension[firstEaExtension] & 0xff;
                    calculate();
                    op.phase = BitOperation::Phase::Prefetch;
                    continue;
                }
                if (!op.addressReady)
                {
                    if (op.mode == 4)
                        state_.address[op.reg] -= step;
                    op.address = moveMemoryAddress(op.mode, op.reg,
                                                   state_.address, state_.data,
                                                   op.extension[firstEaExtension],
                                                   op.extension[firstEaExtension + 1],
                                                   state_.pc + 2 + firstEaExtension * 2);
                    op.addressReady = true;
                }
                if (transfer(op.address, true, false, 0,
                             op.mode == 7 && (op.reg == 2 || op.reg == 3) ? programCode : dataCode) == TransferResult::Complete)
                {
                    op.value = pendingByte_;
                    if (op.mode == 3)
                        state_.address[op.reg] += step;
                    calculate();
                    op.phase = BitOperation::Phase::Prefetch;
                }
                return;

            case BitOperation::Phase::Prefetch:
                if (transfer(state_.pc + (op.extensions + 2) * 2, false,
                             false, 0, programCode) != TransferResult::Complete)
                    return;
                op.nextFollowing = pendingWord_;
                if (op.kind != 0 && op.mode != 0)
                    op.phase = BitOperation::Phase::Write;
                else
                    finish();
                return;

            case BitOperation::Phase::Write:
                if (transfer(op.address, true, true,
                             static_cast<std::uint16_t>(op.result), dataCode) == TransferResult::Complete)
                    finish();
                return;
            }
        }
    }

    void Moto68000::clockRegisterOperation()
    {
        if (registerOperation_.idle)
        {
            --registerOperation_.idle;
            bus_.observe(pins_);
            return;
        }

        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        if (transfer(state_.pc + 4, false, false, 0, programCode) != TransferResult::Complete)
            return;

        const std::uint16_t opcode = state_.prefetch[0];
        const unsigned reg = opcode & 7;
        if ((opcode & 0xfff0) == 0x4e60)
        {
            if (opcode & 0x0008)
                state_.address[reg] = usp_;
            else
                usp_ = state_.address[reg];
        }
        else if ((opcode & 0xf1f8) == 0xc140 ||
                 (opcode & 0xf1f8) == 0xc148 ||
                 (opcode & 0xf1f8) == 0xc188)
        {
            const unsigned other = (opcode >> 9) & 7;
            const unsigned mode = (opcode >> 3) & 0x1f;
            std::uint32_t &first = mode == 9 ? state_.address[other] : state_.data[other];
            std::uint32_t &second = mode == 8 ? state_.data[reg] : state_.address[reg];
            const std::uint32_t previous = first;
            first = second;
            second = previous;
        }
        else
        {
            const std::uint32_t value = state_.data[reg];
            const bool swap = (opcode & 0xfff8) == 0x4840;
            const bool word = (opcode & 0xfff8) == 0x4880;
            const std::uint32_t result = swap ? (value << 16) | (value >> 16) : word ? (value & 0xffff0000u) | static_cast<std::uint16_t>(signedByte(value & 0xff))
                                                                                     : static_cast<std::uint32_t>(signedWord(value & 0xffff));
            const std::uint32_t mask = word ? 0xffffu : 0xffffffffu;
            const std::uint32_t sign = word ? 0x8000u : 0x80000000u;
            state_.data[reg] = result;
            state_.sr = (state_.sr & ~std::uint16_t(0x0f)) |
                        (result & sign ? 0x08 : 0) | ((result & mask) == 0 ? 0x04 : 0);
        }
        state_.prefetch = {state_.prefetch[1], pendingWord_};
        state_.pc += 2;
        ++state_.instructions;
        memoryOp_ = MemoryOp::None;
        tracePending_ = traceInstruction_;
        traceInstruction_ = false;
    }

    void Moto68000::clockShiftRotate()
    {
        auto &op = shiftRotate_;
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        const unsigned dataCode = state_.sr & 0x2000 ? 5 : 1;
        const std::uint16_t opcode = state_.prefetch[0];
        const unsigned kind = op.memory ? (opcode >> 9) & 3 : (opcode >> 3) & 3;
        const bool left = (opcode & 0x0100) != 0;
        if (!op.memory)
        {
            if (op.idle)
            {
                --op.idle;
                bus_.observe(pins_);
                return;
            }
            if (transfer(state_.pc + 4, false, false, 0, programCode) != TransferResult::Complete)
                return;
            const unsigned sizeCode = (opcode >> 6) & 3;
            const unsigned bits = sizeCode == 0 ? 8 : sizeCode == 1 ? 16
                                                                    : 32;
            const std::uint32_t mask = bits == 32 ? 0xffffffffu : (1u << bits) - 1;
            const unsigned reg = opcode & 7;
            const ShiftResult result = shifted(state_.data[reg], bits,
                                               kind, left, op.count, state_.sr);
            state_.data[reg] = (state_.data[reg] & ~mask) | result.value;
            state_.sr = (state_.sr & ~std::uint16_t(0x1f)) | result.flags;
            state_.prefetch = {state_.prefetch[1], pendingWord_};
            state_.pc += 2;
            ++state_.instructions;
            memoryOp_ = MemoryOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
            return;
        }

        const auto finish = [&]()
        {
            state_.sr = (state_.sr & ~std::uint16_t(0x1f)) | op.flags;
            state_.prefetch = {op.nextOpcode, op.nextFollowing};
            state_.pc += 2 + op.extensions * 2;
            ++state_.instructions;
            memoryOp_ = MemoryOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        };
        for (;;)
        {
            switch (op.phase)
            {
            case ShiftRotate::Phase::Extensions:
                if (op.fetchesLeft)
                {
                    if (transfer(state_.pc + op.streamIndex * 2, false, false,
                                 0, programCode) == TransferResult::Complete)
                    {
                        const unsigned index = op.streamIndex++;
                        if (index <= op.extensions)
                            op.extension[index - 1] = pendingWord_;
                        else
                            op.nextOpcode = pendingWord_;
                        --op.fetchesLeft;
                    }
                    return;
                }
                op.phase = ShiftRotate::Phase::Idle;
                continue;

            case ShiftRotate::Phase::Idle:
                if (op.idle)
                {
                    --op.idle;
                    bus_.observe(pins_);
                    return;
                }
                op.phase = ShiftRotate::Phase::Read;
                continue;

            case ShiftRotate::Phase::Read:
                if (!op.addressReady)
                {
                    if (op.mode == 4)
                        state_.address[op.reg] -= 2;
                    op.address = moveMemoryAddress(op.mode, op.reg,
                                                   state_.address, state_.data, op.extension[0],
                                                   op.extension[1], state_.pc + 2);
                    op.addressReady = true;
                }
                if (transfer(op.address, false, false, 0, dataCode) == TransferResult::Complete)
                {
                    if (op.mode == 3)
                        state_.address[op.reg] += 2;
                    const ShiftResult result = shifted(pendingWord_, 16,
                                                       kind, left, 1, state_.sr);
                    op.result = static_cast<std::uint16_t>(result.value);
                    op.flags = result.flags;
                    op.phase = ShiftRotate::Phase::Prefetch;
                }
                return;

            case ShiftRotate::Phase::Prefetch:
                if (transfer(state_.pc + (op.extensions + 2) * 2,
                             false, false, 0, programCode) == TransferResult::Complete)
                {
                    op.nextFollowing = pendingWord_;
                    op.phase = ShiftRotate::Phase::Write;
                }
                return;

            case ShiftRotate::Phase::Write:
                if (transfer(op.address, false, true, op.result, dataCode) == TransferResult::Complete)
                    finish();
                return;
            }
        }
    }

    void Moto68000::clockMovep()
    {
        auto &op = movep_;
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        const unsigned dataCode = state_.sr & 0x2000 ? 5 : 1;
        switch (op.phase)
        {
        case Movep::Phase::NextOpcode:
            if (transfer(state_.pc + 4, false, false, 0, programCode) == TransferResult::Complete)
            {
                op.nextOpcode = pendingWord_;
                op.phase = Movep::Phase::Bytes;
            }
            return;

        case Movep::Phase::Bytes:
        {
            const unsigned shift = op.longSize ? 24 - op.bytesDone * 8 : 8 - op.bytesDone * 8;
            const std::uint16_t byte = op.write ? static_cast<std::uint16_t>((state_.data[op.dataReg] >> shift) & 0xff) : 0;
            if (transfer(op.address + op.bytesDone * 2, true, op.write,
                         byte, dataCode) == TransferResult::Complete)
            {
                if (!op.write)
                    op.value = (op.value << 8) | pendingByte_;
                if (++op.bytesDone == (op.longSize ? 4u : 2u))
                    op.phase = Movep::Phase::FollowingFetch;
            }
            return;
        }

        case Movep::Phase::FollowingFetch:
            if (transfer(state_.pc + 6, false, false, 0, programCode) == TransferResult::Complete)
            {
                if (!op.write)
                    state_.data[op.dataReg] = op.longSize ? op.value : (state_.data[op.dataReg] & 0xffff0000u) | op.value;
                state_.prefetch = {op.nextOpcode, pendingWord_};
                state_.pc += 4;
                ++state_.instructions;
                memoryOp_ = MemoryOp::None;
                tracePending_ = traceInstruction_;
                traceInstruction_ = false;
            }
            return;
        }
    }

    void Moto68000::clockMovem()
    {
        auto &op = movem_;
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        const unsigned dataCode = state_.sr & 0x2000 ? 5 : 1;
        const unsigned operandCode = op.mode == 7 && (op.reg == 2 || op.reg == 3)
                                         ? programCode
                                         : dataCode;
        switch (op.phase)
        {
        case Movem::Phase::Extensions:
            if (op.extensionReads < op.extensions)
            {
                if (transfer(state_.pc + 4 + op.extensionReads * 2, false, false,
                             0, programCode) == TransferResult::Complete)
                    op.extension[op.extensionReads++] = pendingWord_;
                return;
            }
            op.address = moveMemoryAddress(op.mode, op.reg, state_.address,
                                           state_.data, op.extension[0],
                                           op.extension[1], state_.pc + 4);
            op.phase = Movem::Phase::NextOpcode;
            [[fallthrough]];

        case Movem::Phase::NextOpcode:
            if (transfer(state_.pc + 4 + op.extensions * 2, false, false,
                         0, programCode) == TransferResult::Complete)
            {
                op.nextOpcode = pendingWord_;
                op.phase = Movem::Phase::Idle;
            }
            return;

        case Movem::Phase::Idle:
            if (op.idle)
            {
                --op.idle;
                bus_.observe(pins_);
                return;
            }
            op.phase = op.write ? Movem::Phase::Write : Movem::Phase::Read;
            if (op.write)
            {
                clockMovem();
                return;
            }
            [[fallthrough]];

        case Movem::Phase::Read:
        {
            while (op.registerIndex < 16 && !(op.mask & (1u << op.registerIndex)))
                ++op.registerIndex;
            if (op.registerIndex == 16)
            {
                op.phase = Movem::Phase::TrailingRead;
                if (transfer(op.address, false, false, 0, operandCode) == TransferResult::Complete)
                    op.phase = Movem::Phase::FollowingFetch;
                return;
            }
            if (transfer(op.address + op.word * 2, false, false,
                         0, operandCode) != TransferResult::Complete)
                return;
            if (op.size == 4 && op.word == 0)
            {
                op.value = std::uint32_t(pendingWord_) << 16;
                op.word = 1;
                return;
            }
            const std::uint32_t value = op.size == 4
                                            ? op.value | pendingWord_
                                            : static_cast<std::uint32_t>(signedWord(pendingWord_));
            if (op.registerIndex < 8)
                state_.data[op.registerIndex] = value;
            else
                state_.address[op.registerIndex - 8] = value;
            op.address += op.size;
            if (op.mode == 3)
                state_.address[op.reg] = op.address;
            ++op.registerIndex;
            op.word = 0;
            return;
        }

        case Movem::Phase::Write:
        {
            while (op.registerIndex < 16 && !(op.mask & (1u << op.registerIndex)))
                ++op.registerIndex;
            if (op.registerIndex == 16)
            {
                op.phase = Movem::Phase::FollowingFetch;
                return;
            }
            const unsigned registerNumber = op.mode == 4 ? 15 - op.registerIndex
                                                         : op.registerIndex;
            if (!op.writeAddressReady)
            {
                op.value = registerNumber == op.reg + 8 && op.mode == 4
                               ? op.baseValue
                           : registerNumber < 8 ? state_.data[registerNumber]
                                                : state_.address[registerNumber - 8];
                op.writeAddressReady = true;
            }
            const bool lowWord = op.size == 4 && op.mode == 4 ? op.word == 0
                                 : op.size == 4               ? op.word == 1
                                                              : true;
            const std::uint16_t value = static_cast<std::uint16_t>(
                lowWord ? op.value : op.value >> 16);
            const std::uint32_t address = op.mode == 4 ? op.address - 2 : op.address;
            if (transfer(address, false, true, value, dataCode) != TransferResult::Complete)
                return;
            if (op.mode == 4)
            {
                op.address -= 2;
                state_.address[op.reg] = op.address;
            }
            else
                op.address += 2;
            if (op.size == 4 && op.word++ == 0)
                return;
            op.word = 0;
            op.writeAddressReady = false;
            ++op.registerIndex;
            while (op.registerIndex < 16 && !(op.mask & (1u << op.registerIndex)))
                ++op.registerIndex;
            if (op.registerIndex == 16)
                op.phase = Movem::Phase::FollowingFetch;
            return;
        }

        case Movem::Phase::TrailingRead:
            if (transfer(op.address, false, false, 0, operandCode) == TransferResult::Complete)
                op.phase = Movem::Phase::FollowingFetch;
            return;

        case Movem::Phase::FollowingFetch:
            if (transfer(state_.pc + 6 + op.extensions * 2, false, false,
                         0, programCode) == TransferResult::Complete)
            {
                state_.prefetch = {op.nextOpcode, pendingWord_};
                state_.pc += 4 + op.extensions * 2;
                ++state_.instructions;
                memoryOp_ = MemoryOp::None;
                tracePending_ = traceInstruction_;
                traceInstruction_ = false;
            }
            return;
        }
    }

    void Moto68000::clockStatusMove()
    {
        auto &op = statusMove_;
        const unsigned programCode = state_.sr & 0x2000 ? 6 : 2;
        const unsigned dataCode = state_.sr & 0x2000 ? 5 : 1;
        const auto finish = [&]()
        {
            if (op.fromSr && op.mode == 0)
                state_.data[op.reg] = (state_.data[op.reg] & 0xffff0000u) |
                                      op.value;
            state_.prefetch = {op.nextOpcode, op.nextFollowing};
            state_.pc += 2 + op.extensions * 2;
            ++state_.instructions;
            memoryOp_ = MemoryOp::None;
            tracePending_ = traceInstruction_;
            traceInstruction_ = false;
        };

        for (;;)
        {
            switch (op.phase)
            {
            case StatusMove::Phase::Extensions:
                if (op.fetchesLeft)
                {
                    if (transfer(state_.pc + op.streamIndex * 2, false, false,
                                 0, programCode) == TransferResult::Complete)
                    {
                        const unsigned index = op.streamIndex++;
                        if (index <= op.extensions)
                            op.extension[index - 1] = pendingWord_;
                        else
                            op.nextOpcode = pendingWord_;
                        --op.fetchesLeft;
                    }
                    return;
                }
                op.phase = StatusMove::Phase::Read;
                continue;

            case StatusMove::Phase::Read:
                if (op.fromSr && op.mode == 0)
                {
                    op.value = state_.sr;
                    op.phase = StatusMove::Phase::FirstFetch;
                    continue;
                }
                if (!op.fromSr &&
                    (op.mode == 0 || (op.mode == 7 && op.reg == 4)))
                {
                    op.value = op.mode == 0 ? state_.data[op.reg] : op.extension[0];
                    op.phase = StatusMove::Phase::Idle;
                    continue;
                }
                if (op.addressIdle)
                {
                    --op.addressIdle;
                    bus_.observe(pins_);
                    return;
                }
                if (!op.addressReady)
                {
                    if (op.mode == 4)
                        state_.address[op.reg] -= 2;
                    op.address = moveMemoryAddress(op.mode, op.reg,
                                                   state_.address, state_.data, op.extension[0],
                                                   op.extension[1], state_.pc + 2);
                    op.addressReady = true;
                }
                if (transfer(op.address, false, false, 0,
                             !op.fromSr && op.mode == 7 &&
                                     (op.reg == 2 || op.reg == 3)
                                 ? programCode
                                 : dataCode) == TransferResult::Complete)
                {
                    if (op.mode == 3)
                        state_.address[op.reg] += 2;
                    op.value = op.fromSr ? state_.sr : pendingWord_;
                    op.phase = op.fromSr ? StatusMove::Phase::FirstFetch : StatusMove::Phase::Idle;
                }
                return;

            case StatusMove::Phase::Idle:
                if (op.idle)
                {
                    --op.idle;
                    if (op.idle == 0 && !op.fromSr)
                    {
                        if (op.toSr)
                        {
                            const bool switchToUser = (state_.sr & 0x2000) &&
                                                      !(op.value & 0x2000);
                            if (switchToUser)
                            {
                                ssp_ = state_.address[7];
                                state_.address[7] = usp_;
                            }
                            state_.sr = op.value;
                        }
                        else
                            state_.sr = (state_.sr & ~std::uint16_t(0x1f)) |
                                        (op.value & 0x1f);
                    }
                    bus_.observe(pins_);
                    if (op.idle == 0 && op.fromSr)
                        finish();
                    return;
                }
                op.phase = StatusMove::Phase::FirstFetch;
                continue;

            case StatusMove::Phase::FirstFetch:
                if (transfer(state_.pc + (op.extensions + (op.fromSr ? 2 : 1)) * 2,
                             false, false, 0, programCode) == TransferResult::Complete)
                {
                    if (op.fromSr)
                    {
                        op.nextFollowing = pendingWord_;
                        op.phase = op.mode == 0 ? StatusMove::Phase::Idle : StatusMove::Phase::Write;
                    }
                    else
                    {
                        op.nextOpcode = pendingWord_;
                        op.phase = StatusMove::Phase::FollowingFetch;
                    }
                }
                return;

            case StatusMove::Phase::FollowingFetch:
                if (transfer(state_.pc + (op.extensions + 2) * 2,
                             false, false, 0, programCode) == TransferResult::Complete)
                {
                    op.nextFollowing = pendingWord_;
                    finish();
                }
                return;

            case StatusMove::Phase::Write:
                if (transfer(op.address, false, true, op.value, dataCode) == TransferResult::Complete)
                    finish();
                return;
            }
        }
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
        branchDbccExpired_ = false;
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
        combinedMove_ = {};
        testClearCompare_ = {};
        arithmetic_ = {};
        multiplyDivide_ = {};
        extendedArithmetic_ = {};
        statusImmediate_ = {};
        bitOperation_ = {};
        registerOperation_ = {};
        shiftRotate_ = {};
        movep_ = {};
        movem_ = {};
        statusMove_ = {};
        pendingByte_ = 0;
        halted_ = false;
        exceptionActive_ = false;
        exceptionLongFrame_ = false;
        exceptionAccurateAddress_ = false;
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
        if (memoryOp_ == MemoryOp::CombinedMove)
        {
            clockCombinedMove();
            return;
        }
        if (memoryOp_ == MemoryOp::TestClearCompare)
        {
            clockTestClearCompare();
            return;
        }
        if (memoryOp_ == MemoryOp::Arithmetic)
        {
            clockArithmetic();
            return;
        }
        if (memoryOp_ == MemoryOp::MultiplyDivide)
        {
            clockMultiplyDivide();
            return;
        }
        if (memoryOp_ == MemoryOp::ExtendedArithmetic)
        {
            clockExtendedArithmetic();
            return;
        }
        if (memoryOp_ == MemoryOp::StatusImmediate)
        {
            clockStatusImmediate();
            return;
        }
        if (memoryOp_ == MemoryOp::BitOperation)
        {
            clockBitOperation();
            return;
        }
        if (memoryOp_ == MemoryOp::RegisterOperation)
        {
            clockRegisterOperation();
            return;
        }
        if (memoryOp_ == MemoryOp::ShiftRotate)
        {
            clockShiftRotate();
            return;
        }
        if (memoryOp_ == MemoryOp::Movep)
        {
            clockMovep();
            return;
        }
        if (memoryOp_ == MemoryOp::Movem)
        {
            clockMovem();
            return;
        }
        if (memoryOp_ == MemoryOp::StatusMove)
        {
            clockStatusMove();
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
                if (mode != 2 && mode != 5 && mode != 6 &&
                    !(mode == 7 && reg <= 3))
                    throw std::logic_error("Modo de endereçamento de JSR/JMP não implementado");
                controlOp_ = (opcode & 0x0040) ? ControlOp::Jmp : ControlOp::Jsr;
                controlStage_ = 0;
                controlIdle_ = mode == 6 || (mode == 7 && reg == 3)   ? 6
                               : mode == 2 || (mode == 7 && reg == 1) ? 0
                                                                      : 2;
                controlExtensionPending_ = mode == 7 && reg == 1;
                controlIr_ = opcode;
                controlReturn_ = state_.pc + (mode == 2 ? 2 : mode == 7 && reg == 1 ? 6
                                                                                    : 4);
                if (mode == 2)
                    controlTarget_ = state_.address[reg];
                else if (mode == 6 || (mode == 7 && reg == 3))
                    controlTarget_ = moveMemoryAddress(mode, reg, state_.address,
                                                       state_.data, state_.prefetch[1], 0,
                                                       state_.pc + 2);
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
                branchDbccExpired_ = false;
                branchIdle_ = branchRead_ = 0;
            }
            else if ((opcode & 0xf0f8) == 0x50c8)
            {
                const unsigned condition = (opcode >> 8) & 15;
                const unsigned reg = opcode & 7;
                const bool satisfied = branchCondition(condition, state_.sr);
                std::uint16_t count = static_cast<std::uint16_t>(state_.data[reg]);
                if (!satisfied)
                {
                    count = std::uint16_t(count - 1);
                    state_.data[reg] = (state_.data[reg] & 0xffff0000u) | count;
                }
                branchTaken_ = !satisfied && count != 0xffff;
                branchDbccExpired_ = !satisfied && !branchTaken_;
                branchTarget_ = branchTaken_ ? state_.pc + 2 + signedWord(state_.prefetch[1])
                                             : state_.pc + 4;
                branchReadsNeeded_ = branchDbccExpired_ ? 3 : 2;
                branchIdleNeeded_ = satisfied ? 4 : 2;
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
            else if ((opcode & 0xf000) == 0x5000 && ((opcode >> 6) & 3) != 3)
            {
                auto &op = arithmetic_;
                op = {};
                const unsigned sizeCode = (opcode >> 6) & 3;
                op.quickInstruction = true;
                op.quickCount = (opcode >> 9) & 7;
                if (op.quickCount == 0)
                    op.quickCount = 8;
                op.subtract = (opcode & 0x0100) != 0;
                op.size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                            : 4;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.dataReg = op.reg;
                op.addressDestination = op.mode == 1;
                op.memoryDestination = !op.addressDestination;
                if ((op.addressDestination && op.size == 1) ||
                    (op.mode == 7 && op.reg > 1))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::Arithmetic;
                op.extensions = op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, op.size) : 0;
                op.fetchesLeft = op.extensions;
                // Em An a operação é sempre de 32 bits e consome oito ciclos.
                op.idle = op.addressDestination ? 4 : op.mode == 4 || op.mode == 6 ? 2
                                                                                   : 0;
                if (op.extensions)
                    op.extension[0] = state_.prefetch[1];
                else
                    op.nextOpcode = state_.prefetch[1];
                clockArithmetic();
                return;
            }
            else if ((opcode & 0xf1c0) == 0xc0c0 ||
                     (opcode & 0xf1c0) == 0xc1c0 ||
                     (opcode & 0xf1c0) == 0x80c0 ||
                     (opcode & 0xf1c0) == 0x81c0)
            {
                auto &op = multiplyDivide_;
                op = {};
                op.divide = (opcode & 0xf000) == 0x8000;
                op.signedOperation = (opcode & 0x0100) != 0;
                op.destination = (opcode >> 9) & 7;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                if (op.mode == 1 || (op.mode == 7 && op.reg > 4))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::MultiplyDivide;
                op.extensions = op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, 2) : 0;
                op.fetchesLeft = op.extensions;
                op.idle = op.mode == 4 || op.mode == 6 ||
                                  (op.mode == 7 && op.reg == 3)
                              ? 2
                              : 0;
                if (op.extensions)
                    op.extension[0] = state_.prefetch[1];
                else
                    op.nextOpcode = state_.prefetch[1];
                clockMultiplyDivide();
                return;
            }
            else if ((opcode & 0xf1f8) == 0x41e8 ||
                     (opcode & 0xf1ff) == 0x41f8 ||
                     (opcode & 0xf1ff) == 0x41f9 ||
                     (opcode & 0xf1ff) == 0x41fa)
            {
                memoryOp_ = MemoryOp::Lea;
                memoryStage_ = 0;
                memoryValue_ = 0;
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
                move.extensions = moveExtensionWords(move.mode, move.reg, move.size);
                move.firstExtension = state_.prefetch[1];
                move.value = move.store ? state_.data[move.dataReg] : move.immediate ? move.firstExtension
                                                                                     : 0;
                if (!move.immediate && move.extensions == 1)
                    move.address = moveMemoryAddress(move.mode, move.reg,
                                                     state_.address, state_.data, move.firstExtension, 0,
                                                     state_.pc + 2);
                clockExtendedMove();
                return;
            }
            else if (combinedMoveOpcode(opcode))
            {
                memoryOp_ = MemoryOp::CombinedMove;
                auto &move = combinedMove_;
                move = {};
                move.size = (opcode >> 12) == 1 ? 1 : (opcode >> 12) == 2 ? 4
                                                                          : 2;
                move.sourceMode = (opcode >> 3) & 7;
                move.sourceReg = opcode & 7;
                move.targetMode = (opcode >> 6) & 7;
                move.targetReg = (opcode >> 9) & 7;
                move.immediate = move.sourceMode == 7 && move.sourceReg == 4;
                move.sourceExtensions = moveExtensionWords(
                    move.sourceMode, move.sourceReg, move.size);
                move.targetExtensions = moveExtensionWords(
                    move.targetMode, move.targetReg, move.size);
                move.sourceFetchesLeft = move.sourceExtensions;
                move.specialAbsoluteLong =
                    ((move.sourceMode >= 2 && move.sourceMode <= 6) ||
                     (move.sourceMode == 7 && move.sourceReg <= 3)) &&
                    move.targetMode == 7 && move.targetReg == 1;
                move.targetFetchesLeft = move.targetExtensions -
                                         (move.specialAbsoluteLong ? 1 : 0);
                // Pré-decremento e indexação consomem dois ciclos internos adicionais.
                move.sourceIdle = move.sourceMode == 4 || move.sourceMode == 6 ||
                                          (move.sourceMode == 7 && move.sourceReg == 3)
                                      ? 2
                                      : 0;
                move.targetIdle = move.targetMode == 6 ? 2 : 0;
                if (move.sourceExtensions)
                    move.sourceExtension[0] = state_.prefetch[1];
                else if (move.targetExtensions)
                    move.targetExtension[0] = state_.prefetch[1];
                else
                    move.nextOpcode = state_.prefetch[1];
                clockCombinedMove();
                return;
            }
            else if ((opcode & 0xf138) == 0xb108)
            {
                const unsigned sizeCode = (opcode >> 6) & 3;
                if (sizeCode == 3)
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                auto &op = extendedArithmetic_;
                op = {};
                op.compareMemory = op.subtract = op.memory = true;
                op.size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                            : 4;
                op.sourceReg = opcode & 7;
                op.targetReg = (opcode >> 9) & 7;
                op.nextOpcode = state_.prefetch[1];
                memoryOp_ = MemoryOp::ExtendedArithmetic;
                clockExtendedArithmetic();
                return;
            }
            else if (((opcode & 0xff00) == 0x4000 ||
                      (opcode & 0xff00) == 0x4400 ||
                      (opcode & 0xff00) == 0x4600) &&
                     ((opcode >> 6) & 3) != 3)
            {
                auto &op = arithmetic_;
                op = {};
                op.unary = (opcode & 0xff00) == 0x4000 ? Arithmetic::Unary::NegateExtend : (opcode & 0xff00) == 0x4400 ? Arithmetic::Unary::Negate
                                                                                                                       : Arithmetic::Unary::Complement;
                const unsigned sizeCode = (opcode >> 6) & 3;
                op.size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                            : 4;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.dataReg = op.reg;
                op.memoryDestination = op.mode != 0;
                if (op.mode == 1 || (op.mode == 7 && op.reg > 1))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::Arithmetic;
                op.extensions = op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, op.size) : 0;
                op.fetchesLeft = op.extensions;
                op.idle = op.mode == 4 || op.mode == 6 ? 2 : 0;
                if (op.mode == 0 && op.size == 4)
                    op.idle += 2;
                if (op.extensions)
                    op.extension[0] = state_.prefetch[1];
                else
                    op.nextOpcode = state_.prefetch[1];
                clockArithmetic();
                return;
            }
            else if ((((opcode & 0xff00) == 0x4a00 ||
                       (opcode & 0xff00) == 0x4200) &&
                      ((opcode >> 6) & 3) != 3) ||
                     ((opcode & 0xf000) == 0xb000 &&
                      (((opcode >> 6) & 7) <= 3 || ((opcode >> 6) & 7) == 7)))
            {
                auto &op = testClearCompare_;
                op = {};
                const unsigned mode = (opcode >> 3) & 7, reg = opcode & 7;
                const bool compare = (opcode & 0xf000) == 0xb000;
                const unsigned sizeCode = (opcode >> 6) & 3;
                const unsigned compareCode = (opcode >> 6) & 7;
                const bool compareAddress = compare && (compareCode == 3 || compareCode == 7);
                op.kind = compareAddress ? TestClearCompare::Kind::CompareAddress : compare                   ? TestClearCompare::Kind::Compare
                                                                                : (opcode & 0xff00) == 0x4200 ? TestClearCompare::Kind::Clear
                                                                                                              : TestClearCompare::Kind::Test;
                op.size = compare         ? (compareCode == 2 || compareCode == 7 ? 4 : compareCode == 0 ? 1
                                                                                                         : 2)
                          : sizeCode == 0 ? 1
                          : sizeCode == 1 ? 2
                                          : 4;
                const bool validMemory = mode >= 2 && mode <= 6;
                const bool validAbsolute = mode == 7 && reg <= 1;
                const bool validCompareSource = compare && mode == 7 && reg <= 4;
                const bool validAddressRegister = compare && mode == 1 && op.size != 1;
                if (!(mode == 0 || validAddressRegister || validMemory ||
                      validAbsolute || validCompareSource))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::TestClearCompare;
                op.mode = mode;
                op.reg = reg;
                op.destination = (opcode >> 9) & 7;
                op.extensions = mode >= 5 ? moveExtensionWords(mode, reg, op.size) : 0;
                op.fetchesLeft = op.extensions;
                op.idle = mode == 4 || mode == 6 || (mode == 7 && reg == 3) ? 2 : 0;
                if (compareAddress && op.size == 2)
                    op.idle += 2;
                else if (compare && op.size == 4)
                    op.idle += 2;
                else if (op.kind == TestClearCompare::Kind::Clear && mode == 0 &&
                         op.size == 4)
                    op.idle += 2;
                if (op.extensions)
                    op.extension[0] = state_.prefetch[1];
                else
                    op.nextOpcode = state_.prefetch[1];
                clockTestClearCompare();
                return;
            }
            else if ((opcode & 0xffc0) == 0x40c0 ||
                     (opcode & 0xffc0) == 0x44c0 ||
                     (opcode & 0xffc0) == 0x46c0 ||
                     (opcode & 0xffc0) == 0x42c0)
            {
                const unsigned family = opcode & 0xffc0;
                const bool fromSr = family == 0x40c0;
                const unsigned mode = (opcode >> 3) & 7;
                const unsigned reg = opcode & 7;
                const bool valid = family != 0x42c0 && mode != 1 &&
                                   (mode <= 6 || (mode == 7 &&
                                                  reg <= (fromSr ? 1u : 4u)));
                if (!valid)
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                if (family == 0x46c0 && !(state_.sr & 0x2000))
                {
                    beginException(8, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                auto &op = statusMove_;
                op = {};
                op.fromSr = fromSr;
                op.toSr = family == 0x46c0;
                op.mode = mode;
                op.reg = reg;
                op.extensions = mode >= 5 ? moveExtensionWords(mode, reg, 2) : 0;
                op.fetchesLeft = op.extensions;
                if (op.extensions)
                    op.extension[0] = state_.prefetch[1];
                else
                    op.nextOpcode = state_.prefetch[1];
                op.idle = fromSr ? (mode == 0 ? 2 : 0) : 4;
                op.addressIdle = mode == 4 || mode == 6 ||
                                         (mode == 7 && reg == 3)
                                     ? 2
                                     : 0;
                memoryOp_ = MemoryOp::StatusMove;
                clockStatusMove();
                return;
            }
            else if ((opcode & 0xf138) == 0x0108)
            {
                auto &op = movep_;
                op = {};
                op.dataReg = (opcode >> 9) & 7;
                op.addressReg = opcode & 7;
                op.write = (opcode & 0x0080) != 0;
                op.longSize = (opcode & 0x0040) != 0;
                op.address = state_.address[op.addressReg] +
                             signedWord(state_.prefetch[1]);
                memoryOp_ = MemoryOp::Movep;
                clockMovep();
                return;
            }
            else if (((opcode & 0xff80) == 0x4c80 ||
                      (opcode & 0xff80) == 0x4880) &&
                     ((opcode >> 3) & 7) >= 2)
            {
                auto &op = movem_;
                op = {};
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.size = opcode & 0x40 ? 4 : 2;
                op.write = (opcode & 0x0400) == 0;
                const bool valid = op.write
                                       ? (op.mode == 2 || op.mode == 4 || op.mode == 5 ||
                                          op.mode == 6 || (op.mode == 7 && op.reg <= 1))
                                       : (op.mode == 2 || op.mode == 3 || op.mode == 5 ||
                                          op.mode == 6 || (op.mode == 7 && op.reg <= 3));
                if (!valid)
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                op.mask = state_.prefetch[1];
                op.baseValue = state_.address[op.reg];
                op.extensions = op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, op.size) : 0;
                op.idle = op.mode == 6 || (op.mode == 7 && op.reg == 3) ? 2 : 0;
                memoryOp_ = MemoryOp::Movem;
                clockMovem();
                return;
            }
            else if ((opcode & 0xff00) == 0x0800 ||
                     ((opcode & 0xf100) == 0x0100 &&
                      ((opcode >> 3) & 7) != 1))
            {
                auto &op = bitOperation_;
                op = {};
                op.staticBit = (opcode & 0x0100) == 0;
                op.kind = (opcode >> 6) & 3;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.bitReg = (opcode >> 9) & 7;
                const bool memory = op.mode >= 2 && op.mode <= 6;
                const bool absolute = op.mode == 7 && op.reg <= 1;
                const bool readOnly = op.kind == 0 && op.mode == 7 &&
                                      op.reg <= 4;
                if (!(op.mode == 0 || memory || absolute || readOnly))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::BitOperation;
                op.extensions = (op.staticBit ? 1 : 0) +
                                (op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, 1) : 0);
                op.fetchesLeft = op.extensions;
                if (op.extensions)
                    op.extension[0] = state_.prefetch[1];
                else
                    op.nextOpcode = state_.prefetch[1];
                if (op.mode == 0)
                {
                    const unsigned bit = (op.staticBit ? op.extension[0] : state_.data[op.bitReg]) & 31u;
                    op.idle = (op.kind == 0 ? 2 : op.kind == 2 ? 4
                                                               : 2) +
                              (op.kind != 0 && bit >= 16 ? 2 : 0);
                }
                else
                    op.idle = op.mode == 4 || op.mode == 6 ||
                                      (op.mode == 7 && (op.reg == 3 || op.reg == 4))
                                  ? 2
                                  : 0;
                clockBitOperation();
                return;
            }
            else if ((opcode & 0xfff0) == 0x4e60)
            {
                if (!(state_.sr & 0x2000))
                {
                    beginException(8, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::RegisterOperation;
                registerOperation_.idle = 0;
                clockRegisterOperation();
                return;
            }
            else if ((opcode & 0xfff8) == 0x4840 ||
                     (opcode & 0xfff8) == 0x4880 ||
                     (opcode & 0xfff8) == 0x48c0 ||
                     (opcode & 0xf1f8) == 0xc140 ||
                     (opcode & 0xf1f8) == 0xc148 ||
                     (opcode & 0xf1f8) == 0xc188)
            {
                memoryOp_ = MemoryOp::RegisterOperation;
                registerOperation_.idle = (opcode & 0xf000) == 0xc000 ? 2 : 0;
                clockRegisterOperation();
                return;
            }
            else if ((opcode & 0xf000) == 0xe000)
            {
                auto &op = shiftRotate_;
                op = {};
                op.memory = ((opcode >> 6) & 3) == 3;
                if (op.memory)
                {
                    op.mode = (opcode >> 3) & 7;
                    op.reg = opcode & 7;
                    const bool valid = (opcode & 0xf8c0) == 0xe0c0 &&
                                       ((op.mode >= 2 && op.mode <= 6) ||
                                        (op.mode == 7 && op.reg <= 1));
                    if (!valid)
                    {
                        beginException(4, state_.pc, false);
                        bus_.observe(pins_);
                        return;
                    }
                    op.extensions = op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, 2) : 0;
                    op.fetchesLeft = op.extensions;
                    if (op.extensions)
                        op.extension[0] = state_.prefetch[1];
                    else
                        op.nextOpcode = state_.prefetch[1];
                    op.idle = op.mode == 4 || op.mode == 6 ? 2 : 0;
                }
                else
                {
                    const unsigned countField = (opcode >> 9) & 7;
                    op.count = opcode & 0x20 ? state_.data[countField] & 63u : countField ? countField
                                                                                          : 8u;
                    op.idle = (((opcode >> 6) & 3) == 2 ? 4u : 2u) +
                              2u * op.count;
                }
                memoryOp_ = MemoryOp::ShiftRotate;
                clockShiftRotate();
                return;
            }
            else if (opcode == 0x003c || opcode == 0x007c ||
                     opcode == 0x023c || opcode == 0x027c ||
                     opcode == 0x0a3c || opcode == 0x0a7c)
            {
                if ((opcode & 0x0040) && !(state_.sr & 0x2000))
                {
                    beginException(8, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::StatusImmediate;
                statusImmediate_ = {};
                statusImmediate_.opcode = opcode;
                statusImmediate_.immediate = state_.prefetch[1];
                clockStatusImmediate();
                return;
            }
            else if ((opcode & 0xff00) == 0x0c00 &&
                     ((opcode >> 6) & 3) != 3)
            {
                auto &op = arithmetic_;
                op = {};
                op.compareOnly = op.subtract = op.immediateInstruction = true;
                const unsigned sizeCode = (opcode >> 6) & 3;
                op.size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                            : 4;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.dataReg = op.reg;
                op.memoryDestination = op.mode != 0;
                if (op.mode == 1 || (op.mode == 7 && op.reg > 1))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::Arithmetic;
                op.immediateWords = op.size == 4 ? 2 : 1;
                op.extensions = op.immediateWords +
                                (op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, op.size) : 0);
                op.fetchesLeft = op.extensions;
                op.extension[0] = state_.prefetch[1];
                op.idle = op.mode == 4 || op.mode == 6 ? 2 : 0;
                if (!op.memoryDestination && op.size == 4)
                    op.idle += 2;
                clockArithmetic();
                return;
            }
            else if (((opcode & 0xff00) == 0x0000 ||
                      (opcode & 0xff00) == 0x0200 ||
                      (opcode & 0xff00) == 0x0a00) &&
                     ((opcode >> 6) & 3) != 3 &&
                     (opcode & 0x003f) != 0x003c)
            {
                auto &op = arithmetic_;
                op = {};
                op.logic = (opcode & 0xff00) == 0x0000 ? Arithmetic::Logic::Or : (opcode & 0xff00) == 0x0200 ? Arithmetic::Logic::And
                                                                                                             : Arithmetic::Logic::Xor;
                op.immediateInstruction = true;
                const unsigned sizeCode = (opcode >> 6) & 3;
                op.size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                            : 4;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.dataReg = op.reg;
                op.memoryDestination = op.mode != 0;
                if (op.mode == 1 || (op.mode == 7 && op.reg > 1))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::Arithmetic;
                op.immediateWords = op.size == 4 ? 2 : 1;
                op.extensions = op.immediateWords +
                                (op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, op.size) : 0);
                op.fetchesLeft = op.extensions;
                op.extension[0] = state_.prefetch[1];
                op.idle = op.mode == 4 || op.mode == 6 ? 2 : 0;
                if (!op.memoryDestination && op.size == 4)
                    op.idle += 4;
                clockArithmetic();
                return;
            }
            else if (((opcode & 0xf000) == 0x8000 ||
                      (opcode & 0xf000) == 0xc000 ||
                      (opcode & 0xf000) == 0xb000) &&
                     (((opcode >> 6) & 7) <= 2 ||
                      (((opcode >> 6) & 7) >= 4 && ((opcode >> 6) & 7) <= 6)) &&
                     ((opcode & 0xf000) != 0xb000 || ((opcode >> 6) & 7) >= 4) &&
                     (((opcode >> 6) & 7) < 4 || (opcode & 0xf000) == 0xb000 ||
                      ((opcode >> 3) & 7) >= 2))
            {
                auto &op = arithmetic_;
                op = {};
                const unsigned family = opcode & 0xf000;
                const unsigned opmode = (opcode >> 6) & 7;
                op.logic = family == 0x8000 ? Arithmetic::Logic::Or : family == 0xc000 ? Arithmetic::Logic::And
                                                                                       : Arithmetic::Logic::Xor;
                op.memoryDestination = opmode >= 4;
                op.size = opmode == 2 || opmode == 6 ? 4 : opmode == 0 || opmode == 4 ? 1
                                                                                      : 2;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.dataReg = (opcode >> 9) & 7;
                const bool memory = op.mode >= 2 && op.mode <= 6;
                const bool absolute = op.mode == 7 && op.reg <= 1;
                const bool valid = op.memoryDestination ? (memory || absolute || (family == 0xb000 && op.mode == 0)) : (op.mode == 0 || memory || (op.mode == 7 && op.reg <= 4));
                if (!valid)
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::Arithmetic;
                op.extensions = op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, op.size) : 0;
                op.fetchesLeft = op.extensions;
                op.idle = op.mode == 4 || op.mode == 6 ||
                                  (op.mode == 7 && op.reg == 3)
                              ? 2
                              : 0;
                if (op.size == 4)
                {
                    if (op.memoryDestination && op.mode == 0)
                        op.idle += 4;
                    else if (!op.memoryDestination)
                        op.idle += op.mode == 0 ||
                                           (op.mode == 7 && op.reg == 4)
                                       ? 4
                                       : 2;
                }
                if (op.extensions)
                    op.extension[0] = state_.prefetch[1];
                else
                    op.nextOpcode = state_.prefetch[1];
                clockArithmetic();
                return;
            }
            else if (((opcode & 0xff00) == 0x0600 ||
                      (opcode & 0xff00) == 0x0400) &&
                     ((opcode >> 6) & 3) != 3)
            {
                auto &op = arithmetic_;
                op = {};
                const unsigned sizeCode = (opcode >> 6) & 3;
                op.subtract = (opcode & 0xff00) == 0x0400;
                op.immediateInstruction = true;
                op.size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                            : 4;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.dataReg = op.reg;
                op.memoryDestination = op.mode != 0;
                if (op.mode == 1 || (op.mode == 7 && op.reg > 1))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::Arithmetic;
                op.immediateWords = op.size == 4 ? 2 : 1;
                op.extensions = op.immediateWords +
                                (op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, op.size) : 0);
                op.fetchesLeft = op.extensions;
                op.extension[0] = state_.prefetch[1];
                op.idle = op.mode == 4 || op.mode == 6 ? 2 : 0;
                if (!op.memoryDestination && op.size == 4)
                    op.idle += 4;
                clockArithmetic();
                return;
            }
            else if (((opcode & 0xf130) == 0xd100 ||
                      (opcode & 0xf130) == 0x9100) &&
                     ((opcode >> 6) & 3) != 3)
            {
                auto &op = extendedArithmetic_;
                op = {};
                const unsigned sizeCode = (opcode >> 6) & 3;
                op.subtract = (opcode & 0xf000) == 0x9000;
                op.memory = (opcode & 0x0008) != 0;
                op.size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                            : 4;
                op.sourceReg = opcode & 7;
                op.targetReg = (opcode >> 9) & 7;
                op.idle = op.memory ? 2 : op.size == 4 ? 4
                                                       : 0;
                op.nextOpcode = state_.prefetch[1];
                memoryOp_ = MemoryOp::ExtendedArithmetic;
                clockExtendedArithmetic();
                return;
            }
            else if (((opcode & 0xf000) == 0xd000 ||
                      (opcode & 0xf000) == 0x9000) &&
                     (((opcode >> 6) & 7) <= 3 || ((opcode >> 6) & 7) == 7 ||
                      (((opcode >> 6) & 7) >= 4 &&
                       ((opcode >> 3) & 7) >= 2)))
            {
                auto &op = arithmetic_;
                op = {};
                const unsigned opmode = (opcode >> 6) & 7;
                op.subtract = (opcode & 0xf000) == 0x9000;
                op.addressDestination = opmode == 3 || opmode == 7;
                op.memoryDestination = opmode >= 4 && opmode <= 6;
                op.size = opmode == 2 || opmode == 6 || opmode == 7 ? 4 : opmode == 0 || opmode == 4 ? 1
                                                                                                     : 2;
                op.mode = (opcode >> 3) & 7;
                op.reg = opcode & 7;
                op.dataReg = (opcode >> 9) & 7;
                const bool memory = op.mode >= 2 && op.mode <= 6;
                const bool absolute = op.mode == 7 && op.reg <= 1;
                const bool source = op.mode == 0 ||
                                    (op.mode == 1 && op.size != 1) || memory ||
                                    (op.mode == 7 && op.reg <= 4);
                if (!(op.memoryDestination ? (memory || absolute) : source))
                {
                    beginException(4, state_.pc, false);
                    bus_.observe(pins_);
                    return;
                }
                memoryOp_ = MemoryOp::Arithmetic;
                op.extensions = op.mode >= 5 ? moveExtensionWords(op.mode, op.reg, op.size) : 0;
                op.fetchesLeft = op.extensions;
                op.idle = op.mode == 4 || op.mode == 6 ||
                                  (op.mode == 7 && op.reg == 3)
                              ? 2
                              : 0;
                if (!op.memoryDestination)
                {
                    const bool registerOrImmediate = op.mode <= 1 ||
                                                     (op.mode == 7 && op.reg == 4);
                    op.idle += op.size == 2 && op.addressDestination ? 4 : op.size == 4 ? (registerOrImmediate ? 4 : 2)
                                                                                        : 0;
                }
                if (op.extensions)
                    op.extension[0] = state_.prefetch[1];
                else
                    op.nextOpcode = state_.prefetch[1];
                clockArithmetic();
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
            if (memoryMode_ == 4)
                state_.address[addressReg] -= step;
            memoryAddress_ = moveMemoryAddress(memoryMode_, addressReg,
                                               state_.address, state_.data, 0, 0, state_.pc + 2);
            memoryAddressReady_ = true;
        }
        const unsigned dataPart = (dataRead || dataWrite) && memorySize_ == 4 ? (dataRead ? memoryStage_ : memoryStage_ - 1) : 0;
        const bool reverseLongStore = dataWrite && memorySize_ == 4 && memoryMode_ == 4;
        const std::uint32_t address = dataRead || dataWrite        ? memoryAddress_ + (reverseLongStore ? (1 - dataPart) * 2 : dataPart * 2)
                                      : !resetComplete()           ? (resetRead_ < 4 ? resetRead_ * 2 : state_.pc + (resetRead_ - 4) * 2)
                                      : branchActive_              ? branchDbccExpired_
                                                                         ? (branchRead_ == 0 ? state_.pc + 2 + signedWord(state_.prefetch[1])
                                                                                             : state_.pc + 4 + (branchRead_ - 1) * 2)
                                                                         : (branchTaken_ || branchReadsNeeded_ == 2
                                                                                ? branchTarget_ + branchRead_ * 2
                                                                                : state_.pc + 4)
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
                if ((opcode & 7) == 1 && memoryStage_ == 0)
                {
                    memoryValue_ = pendingWord_; // Segunda palavra do endereço absoluto longo.
                    ++memoryStage_;
                }
                else if (memoryStage_++ == 0)
                    memoryPrefetch_ = pendingWord_;
                else if ((opcode & 7) == 1 && memoryStage_ == 2)
                    memoryPrefetch_ = pendingWord_;
                else
                {
                    const unsigned reg = (opcode >> 9) & 7;
                    const int displacement = signedWord(state_.prefetch[1]);
                    state_.address[reg] = ((opcode >> 3) & 7) == 5
                                              ? state_.address[opcode & 7] + displacement
                                          : (opcode & 7) == 1
                                              ? (std::uint32_t(state_.prefetch[1]) << 16) | memoryValue_
                                          : (opcode & 7) == 2 ? state_.pc + 2 + displacement
                                                              : static_cast<std::uint32_t>(displacement);
                    state_.prefetch = {memoryPrefetch_, pendingWord_};
                    state_.pc += (opcode & 7) == 1 ? 6 : 4;
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
            if (branchDbccExpired_)
            {
                if (branchRead_ != 0)
                    state_.prefetch[branchRead_ - 1] = pendingWord_;
            }
            else if (branchTaken_ || branchReadsNeeded_ == 2)
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
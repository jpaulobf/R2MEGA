#include "Core/Board.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace R2MEGA::Core
{
    namespace
    {
        constexpr auto m68kBusDelayInterval = 128 * Scheduler::M68KDivider;
        constexpr auto audioPhaseStep = Board::AudioSampleRate * Scheduler::Z80Divider;
        constexpr auto audioBufferLimit = Board::AudioSampleRate / 4 * Board::AudioChannels;
        constexpr double pcmScale = 1.0 / (32768.0 * Scheduler::MasterClock);
        const double audioHighPass = std::exp(-2.0 * 3.14159265358979323846 * 20.0 / Board::AudioSampleRate);

        [[noreturn]] void notImplemented()
        {
            throw std::logic_error("R2MEGA machine emulation is not implemented yet");
        }

        constexpr std::uint16_t vdpDataByte(std::uint8_t value)
        {
            // Uma escrita de byte na porta de dados entrega o mesmo valor nas duas vias.
            return (std::uint16_t(value) << 8) | value;
        }
    }

    Board::Board() : cpu_(*this), z80_(*this), scheduler_(*this)
    {
        sn76489_.reset();
        vdp_.setDmaReader([this](std::uint32_t address)
                          { return readWord(address); });
    }
    void Board::insertCartridge(Cartridge cartridge)
    {
        if (cartridge.rom().empty())
            throw std::invalid_argument("Cartridge has no ROM image");
        cartridge_ = std::move(cartridge);
        reset();
    }

    void Board::reset()
    {
        if (cartridge_.rom().empty())
            return;
        cartridge_.resetMapper();
        workRam_.clear();
        audioRam_.clear();
        controllers_.reset();
        vdp_.reset();
        sn76489_.reset();
        ym2612_.reset();
        scheduler_.reset();
        audioSamples_.clear();
        audioPhase_ = 0;
        audioArea_.fill(0);
        audioPreviousInput_.fill(0.0);
        audioPreviousOutput_.fill(0.0);
        nextM68KBusDelay_ = m68kBusDelayInterval;
        m68kBusWait_ = 0;
        cpu_.reset();
        z80_.reset();
        z80BusRequested_ = false;
        z80ResetAsserted_ = true;
        z80Bank_ = 0;
        externalInterruptLevel_ = 0;
        cpu_.setInterruptLevel(0);
        driveCartridgeVres(false);
    }

    void Board::setCartridgeVresHandler(std::function<void(bool)> handler)
    {
        cartridgeVresHandler_ = std::move(handler);
    }

    void Board::observe(const M68KPins &pins)
    {
        // A espera periódica pertence ao próximo ciclo de barramento, inclusive
        // na ROM. Com AS já ativo, TAS ainda está no mesmo ciclo indivisível.
        if (pins.active && pins.phase == 0 && !pins.addressStrobe &&
            !pins.interruptAcknowledge && scheduler_.masterCycles() >= nextM68KBusDelay_)
        {
            nextM68KBusDelay_ = scheduler_.masterCycles() + m68kBusDelayInterval;
            const bool vdpWrite = pins.write && pins.address >= 0xc00000 && pins.address <= 0xc00007;
            m68kBusWait_ = vdpWrite ? 0 : 2;
        }
        // O RESET emitido pelo 68000 chega ao conector pela rede /VRES.
        driveCartridgeVres(pins.resetAsserted);
    }

    bool Board::m68kBusReady()
    {
        if (m68kBusWait_ == 0)
            return true;
        --m68kBusWait_;
        return false;
    }

    void Board::driveCartridgeVres(bool asserted)
    {
        if (cartridgeVresAsserted_ == asserted)
            return;
        cartridgeVresAsserted_ = asserted;
        if (asserted)
            cartridge_.resetMapper();
        if (cartridgeVresHandler_)
            cartridgeVresHandler_(asserted);
    }

    void Board::clock() { scheduler_.clock(); }
    void Board::setM68KInterruptLevel(unsigned level)
    {
        if (level > 7)
            throw std::invalid_argument("Nível de interrupção do 68000 fora de 0..7");
        externalInterruptLevel_ = level;
        cpu_.setInterruptLevel(std::max(level, vdp_.interruptLevel()));
    }
    void Board::clockVDP()
    {
        vdp_.clock();
        cpu_.setInterruptLevel(std::max(externalInterruptLevel_, vdp_.interruptLevel()));
    }

    bool Board::tryAcknowledgeInterrupt(unsigned level, M68KInterruptResponse &response)
    {
        if ((level == 6 || level == 4) && vdp_.interruptLevel() == level)
            vdp_.acknowledgeInterrupt(level);
        cpu_.setInterruptLevel(std::max(externalInterruptLevel_, vdp_.interruptLevel()));
        return M68KBus::tryAcknowledgeInterrupt(level, response);
    }
    void Board::clockM68K()
    {
        ym2612_.clock();
        if (!vdp_.dmaBlocksM68K())
            cpu_.clock();
        else if (scheduler_.masterCycles() >= nextM68KBusDelay_)
            nextM68KBusDelay_ = scheduler_.masterCycles() + m68kBusDelayInterval;
    }
    void Board::clockZ80()
    {
        sampleAudio();
        sn76489_.clock();
        if (!z80ResetAsserted_)
        {
            z80_.setIRQ(vdp_.z80InterruptAsserted());
            z80_.tick();
        }
    }

    void Board::sampleAudio()
    {
        // Centraliza o PSG e dá ao FM margem de amplitude antes da soma.
        const auto psg = sn76489_.sample();
        const auto &fm = ym2612_.sample();
        const std::array<std::int64_t, AudioChannels> level{psg + fm[0] * 3 / 4, psg + fm[1] * 3 / 4};
        for (unsigned channel = 0; channel < AudioChannels; ++channel)
            audioArea_[channel] += level[channel] * audioPhaseStep;
        audioPhase_ += audioPhaseStep;
        if (audioPhase_ < Scheduler::MasterClock)
            return;
        audioPhase_ -= Scheduler::MasterClock;
        const bool accept = audioSamples_.size() < audioBufferLimit;
        for (unsigned channel = 0; channel < AudioChannels; ++channel)
        {
            const auto remainder = level[channel] * audioPhase_;
            const double input = (audioArea_[channel] - remainder) * pcmScale;
            audioArea_[channel] = remainder;
            const double output = input - audioPreviousInput_[channel] + audioHighPass * audioPreviousOutput_[channel];
            audioPreviousInput_[channel] = input;
            audioPreviousOutput_[channel] = output;
            if (accept)
                audioSamples_.push_back(static_cast<float>(std::clamp(output, -1.0, 1.0)));
        }
    }

    void Board::takeAudio(std::vector<float> &samples)
    {
        samples.clear();
        samples.swap(audioSamples_);
    }
    bool Board::z80BusGranted() const
    {
        return z80ResetAsserted_ ||
               (z80BusRequested_ && (z80_.pins().signals & Z80Pins::BusAck));
    }
    void Board::setZ80BusRequest(bool requested)
    {
        z80BusRequested_ = requested;
        z80_.setBusRequest(requested);
    }
    void Board::setZ80Reset(bool asserted)
    {
        if (asserted && !z80ResetAsserted_)
        {
            z80_.reset();
            z80_.setBusRequest(z80BusRequested_);
            ym2612_.reset();
        }
        z80ResetAsserted_ = asserted;
    }
    void Board::writeZ80BankBit(std::uint8_t value)
    {
        // Nove escritas seriais formam A15-A23, começando pelo bit menos significativo.
        z80Bank_ = std::uint16_t((z80Bank_ >> 1) | ((value & 1) << 8));
    }
    std::uint8_t Board::readByte(std::uint32_t address)
    {
        if (address <= Cartridge::windowEnd)
            return cartridge_.readByte(address);
        if (address >= 0xa10000 && address <= 0xa1001f)
            return controllers_.readByte(address - 0xa10000);
        if (address >= 0xa00000 && address <= 0xa03fff)
        {
            if (!z80BusGranted())
                throw std::logic_error("Barramento do Z80 ainda não concedido ao 68000");
            return audioRam_.read(address & 0x1fff);
        }
        if (address >= 0xa04000 && address < 0xa06000)
        {
            if (!z80BusGranted())
                throw std::logic_error("Barramento do Z80 ainda não concedido ao 68000");
            return ym2612_.readPort(address & 3);
        }
        if (address == 0xa11100)
            return z80BusGranted() ? 0 : 1;
        if (address == 0xa11101)
            return 0;
        if (address >= 0xc00004 && address <= 0xc00007)
        {
            const auto status = vdp_.readStatus();
            return (address & 1) ? std::uint8_t(status) : std::uint8_t(status >> 8);
        }
        if (address == 0xc00008 || address == 0xc00009)
        {
            const auto counter = vdp_.readHvCounter();
            return (address & 1) ? std::uint8_t(counter) : std::uint8_t(counter >> 8);
        }
        // Os 64 KiB físicos aparecem repetidos na faixa E00000-FFFFFF.
        if (address >= 0xe00000 && address <= 0xffffff)
            return workRam_.read(address & 0xffff);
        notImplemented();
    }

    std::uint16_t Board::readWord(std::uint32_t address)
    {
        if (address & 1)
            throw std::invalid_argument("Unaligned 68000 word read");
        if (address >= 0xa00000 && address < 0xa06000)
        {
            const auto value = readByte(address);
            return (std::uint16_t(value) << 8) | value;
        }
        if (address == 0xa11100)
            return z80BusGranted() ? 0 : 0x0100;
        if (address == 0xc00004 || address == 0xc00006)
            return vdp_.readStatus();
        if (address == 0xc00008)
            return vdp_.readHvCounter();
        if (address == 0xc00000 || address == 0xc00002)
            return vdp_.readData();
        return (std::uint16_t(readByte(address)) << 8) | readByte(address + 1);
    }

    bool Board::tryReadByte(std::uint32_t address, std::uint8_t &value)
    {
        return m68kBusReady() && M68KBus::tryReadByte(address, value);
    }

    bool Board::tryReadWord(std::uint32_t address, std::uint16_t &value)
    {
        return m68kBusReady() && M68KBus::tryReadWord(address, value);
    }

    void Board::writeByte(std::uint32_t address, std::uint8_t value)
    {
        if (address <= Cartridge::windowEnd)
        {
            cartridge_.writeByte(address, value);
            return;
        }
        if (address == Cartridge::saveControlAddress)
        {
            cartridge_.writeSaveControl(value);
            return;
        }
        if (address == Cartridge::saveControlAddress - 1)
            return; // O registrador usa somente o byte baixo.
        if (address >= 0xc00000 && address <= 0xc00003)
        {
            vdp_.writeData(vdpDataByte(value));
            return;
        }
        if (address >= 0xa04000 && address < 0xa06000)
        {
            if (!z80BusGranted())
                throw std::logic_error("Barramento do Z80 ainda não concedido ao 68000");
            ym2612_.writePort(address & 3, value);
            return;
        }
        if (address >= 0xa06000 && address <= 0xa060ff && !(address & 1))
        {
            if (!z80BusGranted())
                throw std::logic_error("Barramento do Z80 ainda não concedido ao 68000");
            writeZ80BankBit(value);
            return;
        }
        if (address >= 0xc00010 && address <= 0xc00017)
        {
            if (address & 1)
                sn76489_.write(value);
            return;
        }
        if (address >= 0xa00000 && address <= 0xa03fff)
        {
            if (!z80BusGranted())
                throw std::logic_error("Barramento do Z80 ainda não concedido ao 68000");
            audioRam_.write(address & 0x1fff, value);
            return;
        }
        if (address == 0xa11100)
        {
            setZ80BusRequest((value & 1) != 0);
            return;
        }
        if (address == 0xa11101 || address == 0xa11201)
            return; // O registrador usa somente o byte alto.
        if (address == 0xa11200)
        {
            setZ80Reset((value & 1) == 0);
            return;
        }
        if (address >= 0xa10000 && address <= 0xa1001f)
        {
            controllers_.writeByte(address - 0xa10000, value);
            return;
        }
        if (address >= 0xe00000 && address <= 0xffffff)
        {
            workRam_.write(address & 0xffff, value);
            return;
        }
        notImplemented();
    }

    void Board::writeWord(std::uint32_t address, std::uint16_t value)
    {
        if (address & 1)
            throw std::invalid_argument("Unaligned 68000 word write");
        if (address == Cartridge::saveControlAddress - 1)
        {
            writeByte(Cartridge::saveControlAddress, std::uint8_t(value));
            return;
        }
        if (address <= Cartridge::windowEnd)
        {
            cartridge_.writeByte(address, std::uint8_t(value >> 8));
            cartridge_.writeByte(address + 1, std::uint8_t(value));
            return;
        }
        if (address >= 0xa04000 && address < 0xa06000)
        {
            writeByte(address, std::uint8_t(value >> 8));
            return;
        }
        if (address >= 0xa06000 && address <= 0xa060fe)
        {
            writeByte(address, std::uint8_t(value >> 8));
            return;
        }
        if (address >= 0xc00010 && address <= 0xc00016)
        {
            sn76489_.write(std::uint8_t(value));
            return;
        }
        if (address >= 0xa00000 && address <= 0xa03ffe)
        {
            writeByte(address, std::uint8_t(value >> 8));
            return;
        }
        if (address == 0xa11100 || address == 0xa11200)
        {
            writeByte(address, std::uint8_t(value >> 8));
            return;
        }
        if (address == 0xc00004 || address == 0xc00006)
        {
            vdp_.writeControl(value);
            return;
        }
        if (address == 0xc00000 || address == 0xc00002)
        {
            vdp_.writeData(value);
            return;
        }
        if (address >= 0xa10000 && address <= 0xa1001e)
        {
            controllers_.writeByte(address + 1 - 0xa10000, static_cast<std::uint8_t>(value));
            return;
        }
        if (address >= 0xe00000 && address < 0xffffff)
        {
            writeByte(address, std::uint8_t(value >> 8));
            writeByte(address + 1, std::uint8_t(value));
            return;
        }
        notImplemented();
    }

    bool Board::tryWriteByte(std::uint32_t address, std::uint8_t value)
    {
        if (!m68kBusReady())
            return false;
        if (address >= 0xc00000 && address <= 0xc00003)
            return vdp_.tryWriteData(vdpDataByte(value));
        writeByte(address, value);
        return true;
    }

    bool Board::tryWriteWord(std::uint32_t address, std::uint16_t value)
    {
        if (!m68kBusReady())
            return false;
        if (address == 0xc00000 || address == 0xc00002)
            return vdp_.tryWriteData(value);
        writeWord(address, value);
        return true;
    }
    std::uint8_t Board::readMemory(std::uint16_t address) const
    {
        if (address < 0x4000)
            return audioRam_.read(address & 0x1fff);
        if (address >= 0x4000 && address < 0x6000)
            return ym2612_.readPort(address & 3);
        if (address >= 0x8000)
        {
            const auto mapped = (std::uint32_t(z80Bank_) << 15) | (address & 0x7fff);
            if (mapped <= Cartridge::windowEnd)
                return cartridge_.readByte(mapped);
            if (mapped >= 0xe00000)
                return workRam_.read(mapped & 0xffff);
        }
        notImplemented();
    }
    void Board::writeMemory(std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0x4000 && address < 0x6000)
        {
            ym2612_.writePort(address & 3, value);
            return;
        }
        if (address >= 0x6000 && address <= 0x60ff)
        {
            writeZ80BankBit(value);
            return;
        }
        if (address >= 0x8000)
        {
            const auto mapped = (std::uint32_t(z80Bank_) << 15) | (address & 0x7fff);
            if (mapped <= Cartridge::windowEnd)
            {
                cartridge_.writeByte(mapped, value);
                return;
            }
            if (mapped >= 0xe00000)
            {
                workRam_.write(mapped & 0xffff, value);
                return;
            }
            notImplemented();
        }
        if (address == 0x7f11 || address == 0x7f13 ||
            address == 0x7f15 || address == 0x7f17)
        {
            sn76489_.write(value);
            return;
        }
        if (address < 0x4000)
        {
            audioRam_.write(address & 0x1fff, value);
            return;
        }
        notImplemented();
    }
    // No modo Genesis, o Z80 acessa os periféricos pelo espaço de memória.
    std::uint8_t Board::input(std::uint16_t) { return 0xff; }
    void Board::output(std::uint16_t, std::uint8_t) {}
}
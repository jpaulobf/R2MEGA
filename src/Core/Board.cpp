#include "Core/Board.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace R2MEGA::Core
{
    namespace
    {
        [[noreturn]] void notImplemented()
        {
            throw std::logic_error("R2MEGA machine emulation is not implemented yet");
        }
    }

    Board::Board() : cpu_(*this), z80_(*this), scheduler_(*this)
    {
        sn76489_.reset();
        ym2612_.reset();
        vdp_.setDmaReader([this](std::uint32_t address)
                          { return readWord(address); });
    }
    void Board::insertCartridge(Cartridge cartridge)
    {
        if (cartridge.rom().empty())
            throw std::invalid_argument("Cartridge has no ROM image");
        cartridge_ = std::move(cartridge);
        workRam_.clear();
        audioRam_.clear();
        controllers_.reset();
        vdp_.reset();
        sn76489_.reset();
        ym2612_.reset();
        scheduler_.reset();
        cpu_.reset();
        z80_.reset();
        z80BusRequested_ = false;
        z80ResetAsserted_ = true;
        z80Bank_ = 0;
        externalInterruptLevel_ = 0;
        cpu_.setInterruptLevel(0);
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
        if (level == 6 && vdp_.interruptLevel() == 6)
            vdp_.acknowledgeInterrupt(level);
        cpu_.setInterruptLevel(std::max(externalInterruptLevel_, vdp_.interruptLevel()));
        return M68KBus::tryAcknowledgeInterrupt(level, response);
    }
    void Board::clockM68K()
    {
        if (!vdp_.dmaBlocksM68K())
            cpu_.clock();
    }
    void Board::clockZ80()
    {
        sn76489_.clock();
        if (!z80ResetAsserted_)
            z80_.tick();
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
        if (address <= 0x3fffff)
            return cartridge_.rom().at(address);
        if (address >= 0xa10000 && address <= 0xa1001f)
            return controllers_.readByte(address - 0xa10000);
        if (address >= 0xa00000 && address <= 0xa03fff)
        {
            if (!z80BusGranted())
                throw std::logic_error("Barramento do Z80 ainda não concedido ao 68000");
            return audioRam_.read(address & 0x1fff);
        }
        if (address >= 0xa04000 && address <= 0xa04003)
        {
            if (!z80BusGranted())
                throw std::logic_error("Barramento do Z80 ainda não concedido ao 68000");
            return static_cast<std::uint8_t>(ym2612_.readStatus());
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
        // Os 64 KiB físicos aparecem repetidos na faixa E00000-FFFFFF.
        if (address >= 0xe00000 && address <= 0xffffff)
            return workRam_.read(address & 0xffff);
        notImplemented();
    }

    std::uint16_t Board::readWord(std::uint32_t address)
    {
        if (address & 1)
            throw std::invalid_argument("Unaligned 68000 word read");
        if (address >= 0xa00000 && address <= 0xa03ffe)
        {
            const auto value = readByte(address);
            return (std::uint16_t(value) << 8) | value;
        }
        if (address == 0xa11100)
            return z80BusGranted() ? 0 : 0x0100;
        if (address == 0xc00004 || address == 0xc00006)
            return vdp_.readStatus();
        if (address == 0xc00000 || address == 0xc00002)
            return vdp_.readData();
        return (std::uint16_t(readByte(address)) << 8) | readByte(address + 1);
    }

    void Board::writeByte(std::uint32_t address, std::uint8_t value)
    {
        if (address >= 0xa04000 && address <= 0xa04003)
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
        if (address == 0xa04000 || address == 0xa04002)
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
    std::uint8_t Board::readMemory(std::uint16_t address) const
    {
        if (address < 0x4000)
            return audioRam_.read(address & 0x1fff);
        if (address >= 0x4000 && address < 0x6000)
            return static_cast<std::uint8_t>(ym2612_.readStatus());
        if (address >= 0x8000)
        {
            const auto mapped = (std::uint32_t(z80Bank_) << 15) | (address & 0x7fff);
            if (mapped <= 0x3fffff)
                return cartridge_.rom().at(mapped);
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
            if (mapped >= 0xe00000)
            {
                workRam_.write(mapped & 0xffff, value);
                return;
            }
            // Outros dispositivos e a SRAM de cartuchos ainda não passam por esta janela.
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
    std::uint8_t Board::input(std::uint16_t) { notImplemented(); }
    void Board::output(std::uint16_t, std::uint8_t) { notImplemented(); }
}
#include "Core/Board.h"
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
        vdp_.setDmaReader([this](std::uint32_t address)
                          { return readWord(address); });
    }
    void Board::insertCartridge(Cartridge cartridge)
    {
        if (cartridge.rom().empty())
            throw std::invalid_argument("Cartridge has no ROM image");
        cartridge_ = std::move(cartridge);
        workRam_.clear();
        controllers_.reset();
        vdp_.reset();
        scheduler_.reset();
        cpu_.reset();
    }

    void Board::clock() { scheduler_.clock(); }
    void Board::setM68KInterruptLevel(unsigned level) { cpu_.setInterruptLevel(level); }
    void Board::clockVDP() { vdp_.clock(); }
    void Board::clockM68K()
    {
        if (!vdp_.dmaBlocksM68K())
            cpu_.clock();
    }
    // A CPU será conectada ao scheduler após a implementação do barramento do Mega Drive.
    void Board::clockZ80() {}
    std::uint8_t Board::readByte(std::uint32_t address)
    {
        if (address <= 0x3fffff)
            return cartridge_.rom().at(address);
        if (address >= 0xa10000 && address <= 0xa1001f)
            return controllers_.readByte(address - 0xa10000);
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
        if (address == 0xc00004 || address == 0xc00006)
            return vdp_.readStatus();
        if (address == 0xc00000 || address == 0xc00002)
            return vdp_.readData();
        return (std::uint16_t(readByte(address)) << 8) | readByte(address + 1);
    }

    void Board::writeByte(std::uint32_t address, std::uint8_t value)
    {
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
    std::uint8_t Board::readMemory(std::uint16_t) const { notImplemented(); }
    void Board::writeMemory(std::uint16_t, std::uint8_t) { notImplemented(); }
    std::uint8_t Board::input(std::uint16_t) { notImplemented(); }
    void Board::output(std::uint16_t, std::uint8_t) { notImplemented(); }
}
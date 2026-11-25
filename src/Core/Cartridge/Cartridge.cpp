#include "Core/Cartridge/Cartridge.h"
#include "Common/RomFile.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace R2MEGA::Core
{
    Cartridge Cartridge::load(const std::filesystem::path &path)
    {
        Cartridge result;
        result.path_ = std::filesystem::absolute(path).lexically_normal();
        result.rom_ = Common::readRomFile(result.path_, &result.romName_);
        auto extension = std::filesystem::u8path(result.romName_).extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        if (extension != ".md" && extension != ".gen" && extension != ".bin" && extension != ".smd")
            throw std::runtime_error("Unsupported ROM extension.");
        if (extension == ".smd")
        {
            // Remove o cabeçalho SMD e desfaz o entrelaçamento de cada bloco de 16 KiB.
            constexpr std::size_t headerSize = 512, blockSize = 16384, half = blockSize / 2;
            if (result.rom_.size() <= headerSize || (result.rom_.size() - headerSize) % blockSize != 0)
                throw std::runtime_error("Invalid SMD ROM: expected a 512-byte header and complete 16 KiB blocks.");
            std::vector<std::uint8_t> decoded(result.rom_.size() - headerSize);
            for (std::size_t block = 0; block < decoded.size(); block += blockSize)
                for (std::size_t i = 0; i < half; ++i)
                {
                    decoded[block + 2 * i] = result.rom_[headerSize + block + half + i];
                    decoded[block + 2 * i + 1] = result.rom_[headerSize + block + i];
                }
            result.rom_ = std::move(decoded);
        }
        return result;
    }

    std::optional<Cartridge::Header> Cartridge::header() const
    {
        constexpr std::size_t headerSize = 0x100;
        if (rom_.size() < Header::offset + headerSize ||
            !std::equal(rom_.begin() + Header::offset, rom_.begin() + Header::offset + 4, "SEGA"))
            return std::nullopt;

        const auto text = [this](std::size_t offset, std::size_t length)
        {
            const auto *first = rom_.data() + offset;
            std::string field(first, std::find(first, first + length, std::uint8_t(0)));
            for (auto &character : field)
                if (character < ' ' || character > '~')
                    character = '?';
            const auto start = field.find_first_not_of(' ');
            if (start == std::string::npos)
                return std::string{};
            return field.substr(start, field.find_last_not_of(' ') - start + 1);
        };

        return Header{text(0x100, 16), text(0x120, 48), text(0x150, 48),
                      text(0x180, 14), text(0x1f0, 16),
                      std::uint16_t((std::uint16_t(rom_[0x18e]) << 8) | rom_[0x18f])};
    }
}
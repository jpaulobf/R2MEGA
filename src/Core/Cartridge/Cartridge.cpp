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
}
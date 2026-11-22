#include "Window/KeyboardConfig.h"
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace R2MEGA::Window
{
    Systems::KeyboardMapping KeyboardConfig::load()
    {
        auto mapping = Systems::defaultKeyboardMapping();
        canSave_ = true;
        std::ifstream file(path_, std::ios::binary | std::ios::ate);
        if (!file)
        {
            std::error_code error;
            canSave_ = !std::filesystem::exists(path_, error) && !error;
            return mapping;
        }
        const auto size = file.tellg();
        if (size < 0 || size > 4096)
        {
            canSave_ = false;
            return mapping;
        }
        file.seekg(0);
        std::string line;
        for (unsigned player = 0; player < 2; ++player)
            for (unsigned button = 0; button < 8 && std::getline(file, line); ++button)
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                unsigned value = 0;
                const auto parsed = std::from_chars(line.data(), line.data() + line.size(), value);
                if (!line.empty() && parsed.ec == std::errc{} && parsed.ptr == line.data() + line.size() &&
                    (value == 0 || Systems::assignableKey(value)))
                    mapping[player][button] = value;
            }
        if (file.bad())
        {
            canSave_ = false;
            return Systems::defaultKeyboardMapping();
        }
        return mapping;
    }

    bool KeyboardConfig::save(const Systems::KeyboardMapping &mapping) const
    {
        if (!canSave_)
            return false;
        std::filesystem::path temporary;
        try
        {
            if (path_.has_parent_path())
                std::filesystem::create_directories(path_.parent_path());
            temporary = path_;
            temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file)
                    return false;
                for (const auto &player : mapping)
                    for (const auto key : player)
                        file << key << '\n';
                file.close();
                if (!file)
                    throw std::runtime_error("Falha ao gravar configuração do teclado");
            }
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Falha ao substituir configuração do teclado");
#else
            std::filesystem::rename(temporary, path_);
#endif
            return true;
        }
        catch (const std::exception &error)
        {
            std::error_code ignored;
            if (!temporary.empty())
                std::filesystem::remove(temporary, ignored);
            std::cerr << "R2MEGA: " << error.what() << '\n';
            return false;
        }
    }
}
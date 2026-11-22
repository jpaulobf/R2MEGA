#pragma once
#include "Systems/KeyboardMapping.h"
#include <filesystem>

namespace R2MEGA::Window
{
    // Lê e grava as teclas confirmadas sem misturar persistência com o diálogo.
    class KeyboardConfig
    {
    public:
        explicit KeyboardConfig(std::filesystem::path path = "resources/keyboard.ini") : path_(std::move(path)) {}
        Systems::KeyboardMapping load();
        bool save(const Systems::KeyboardMapping &mapping) const;

    private:
        std::filesystem::path path_;
        bool canSave_ = true;
    };
}
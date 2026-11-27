#include "Window/FileManager.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace R2MEGA::Window
{
    namespace
    {
        const std::filesystem::path configPath = "resources/config.ini";
        constexpr std::array<std::string_view, 10> recentKeys = {
            "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10"};
        constexpr std::string_view directoryKey = "last_rom_path";

        bool isRomPath(const std::filesystem::path &path)
        {
            auto extension = path.extension().u8string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return extension == ".md" || extension == ".gen" || extension == ".bin" ||
                   extension == ".smd" || extension == ".zip";
        }
    }

    FileManager::FileManager(OpenHandler handler) : openHandler_(std::move(handler))
    {
        readConfig();
    }

    void FileManager::readConfig()
    {
        std::ifstream input(configPath, std::ios::binary);
        std::array<std::filesystem::path, recentKeys.size()> recent;
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto separator = line.find('=');
            if (separator == std::string::npos)
                continue;
            const auto key = std::string_view(line).substr(0, separator);
            const auto value = std::string_view(line).substr(separator + 1);
            try
            {
                if (key == directoryKey)
                    lastRomDirectory_ = std::filesystem::u8path(value);
                else if (const auto found = std::find(recentKeys.begin(), recentKeys.end(), key); found != recentKeys.end())
                {
                    auto &path = recent[found - recentKeys.begin()];
                    path = std::filesystem::u8path(value).lexically_normal().make_preferred();
                    if (!isRomPath(path))
                        path.clear();
                }
            }
            catch (const std::filesystem::filesystem_error &)
            {
                // Uma entrada inválida não impede a leitura das demais.
            }
        }
        for (const auto &path : recent)
            if (!path.empty() && std::find(recentFiles_.begin(), recentFiles_.end(), path) == recentFiles_.end())
                recentFiles_.push_back(path);
    }

    void FileManager::saveConfig() const
    {
        std::vector<std::string> lines;
        {
            std::ifstream input(configPath, std::ios::binary);
            if (!input && std::filesystem::exists(configPath))
                throw std::runtime_error("Não foi possível ler config.ini");
            std::string line;
            while (std::getline(input, line))
            {
                const auto separator = line.find('=');
                const auto key = std::string_view(line).substr(0, separator);
                if (separator == std::string::npos ||
                    (key != directoryKey && std::find(recentKeys.begin(), recentKeys.end(), key) == recentKeys.end()))
                    lines.push_back(line);
            }
            if (input.bad())
                throw std::runtime_error("Não foi possível ler config.ini");
        }
        std::filesystem::create_directories(configPath.parent_path());
        auto temporary = configPath;
        temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        try
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            for (const auto &line : lines)
                output << line << '\n';
            for (std::size_t i = 0; i < recentKeys.size(); ++i)
                output << recentKeys[i] << '=' << (i < recentFiles_.size() ? recentFiles_[i].u8string() : "") << '\n';
            output << directoryKey << '=' << lastRomDirectory_.u8string() << '\n';
            output.close();
            if (!output)
                throw std::runtime_error("Não foi possível gravar config.ini");
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), configPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "config.ini");
#else
            std::filesystem::rename(temporary, configPath);
#endif
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }

    void FileManager::navigate(const std::filesystem::path &path)
    {
        auto directory = std::filesystem::absolute(path).lexically_normal();
        std::vector<Entry> entries;
        for (const auto &item : std::filesystem::directory_iterator(directory))
        {
            std::error_code error;
            const bool isDirectory = item.is_directory(error);
            if (error || (!isDirectory && !item.is_regular_file(error)) || error)
                continue;
            if (!isDirectory && !isRomPath(item.path()))
                continue;
            entries.push_back({item.path(), item.path().filename().u8string(), isDirectory});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b)
                  { return a.directory != b.directory ? a.directory > b.directory : a.name < b.name; });
        directory_ = std::move(directory);
        entries_ = std::move(entries);
        error_.clear();
    }

    void FileManager::browseLastDirectory()
    {
        for (const auto *path : {&lastRomDirectory_, &directory_})
        {
            if (path->empty() || (path == &directory_ && directory_ == lastRomDirectory_))
                continue;
            try
            {
                navigate(*path);
                return;
            }
            catch (const std::filesystem::filesystem_error &)
            {
                // A pasta pode ter sido removida ou estar inacessível.
            }
        }
        directory_.clear();
        entries_.clear();
        try
        {
            navigate(std::filesystem::current_path());
        }
        catch (const std::exception &error)
        {
            error_ = error.what();
        }
    }

    void FileManager::requestOpen(std::filesystem::path file)
    {
        requested_ = true;
        pendingFile_ = std::move(file);
        error_.clear();
        if (pendingFile_.empty())
            browseLastDirectory();
    }

    void FileManager::load(const std::filesystem::path &path)
    {
        if (!openHandler_)
            throw std::runtime_error("ROM loading is unavailable.");
        const auto absolute = std::filesystem::absolute(path).lexically_normal().make_preferred();
        openHandler_(absolute);
        try
        {
            recentFiles_.erase(std::remove(recentFiles_.begin(), recentFiles_.end(), absolute), recentFiles_.end());
            recentFiles_.insert(recentFiles_.begin(), absolute);
            if (recentFiles_.size() > recentKeys.size())
                recentFiles_.resize(recentKeys.size());
            lastRomDirectory_ = absolute.parent_path();
            saveConfig();
        }
        catch (const std::exception &error)
        {
            std::cerr << "R2MEGA: " << error.what() << '\n';
        }
        error_.clear();
        open_ = false;
        ImGui::CloseCurrentPopup();
    }

    void FileManager::draw()
    {
        if (requested_)
        {
            ImGui::OpenPopup("Open ROM");
            requested_ = false;
        }
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(720, 580), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(320.0f, viewport->WorkSize.x), std::min(300.0f, viewport->WorkSize.y)), viewport->WorkSize);
        bool visible = true;
        open_ = ImGui::BeginPopupModal("Open ROM", &visible);
        if (!open_)
            return;
        if (!pendingFile_.empty())
        {
            const auto file = std::exchange(pendingFile_, {});
            try
            {
                load(file);
                ImGui::EndPopup();
                return;
            }
            catch (const std::exception &error)
            {
                browseLastDirectory();
                error_ = error.what();
            }
        }
        ImGui::TextUnformatted("Select a ROM: .md, .gen, .bin, .smd, .zip");
#ifdef _WIN32
        const auto root = directory_.root_name().u8string();
        ImGui::SetNextItemWidth(80);
        if (ImGui::BeginCombo("Drive", root.empty() ? "Select" : root.c_str()))
        {
            const DWORD drives = GetLogicalDrives();
            for (unsigned i = 0; i < 26; ++i)
                if (drives & (DWORD{1} << i))
                {
                    const char drive[] = {static_cast<char>('A' + i), ':', '\\', '\0'};
                    if (ImGui::Selectable(drive))
                        try
                        {
                            navigate(drive);
                        }
                        catch (const std::exception &error)
                        {
                            error_ = error.what();
                        }
                }
            ImGui::EndCombo();
        }
#endif
        ImGui::TextWrapped("Path: %s", directory_.u8string().c_str());
        if (ImGui::Button("Up"))
            try
            {
                if (!directory_.empty())
                    navigate(directory_.parent_path());
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            try
            {
                navigate(directory_.empty() ? std::filesystem::current_path() : directory_);
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
        std::optional<Entry> selection;
        const float errorHeight = error_.empty() ? 0.0f : ImGui::CalcTextSize(error_.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("Files", ImVec2(0, std::max(40.0f, ImGui::GetContentRegionAvail().y - errorHeight)), ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(entries_.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const auto &entry = entries_[static_cast<std::size_t>(i)];
                    ImGui::PushID(i);
                    const bool clicked = ImGui::Selectable("##entry", false, ImGuiSelectableFlags_NoAutoClosePopups);
                    const auto label = (entry.directory ? "[DIR] " : "") + entry.name;
                    ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
                    if (clicked)
                        selection = entry;
                    ImGui::PopID();
                }
        }
        ImGui::EndChild();
        if (selection)
        {
            try
            {
                if (selection->directory)
                    navigate(selection->path);
                else
                    load(selection->path);
            }
            catch (const std::exception &error)
            {
                error_ = error.what();
            }
        }
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndPopup();
    }
}
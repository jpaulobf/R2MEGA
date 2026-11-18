#include "Window/FileManager.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace R2MEGA::Window
{
    FileManager::FileManager(OpenHandler handler) : openHandler_(std::move(handler)) {}

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
            auto extension = item.path().extension().u8string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            if (!isDirectory && extension != ".md" && extension != ".gen" && extension != ".bin" && extension != ".smd" && extension != ".zip")
                continue;
            entries.push_back({item.path(), item.path().filename().u8string(), isDirectory});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b)
                  { return a.directory != b.directory ? a.directory > b.directory : a.name < b.name; });
        directory_ = std::move(directory);
        entries_ = std::move(entries);
        error_.clear();
    }

    void FileManager::requestOpen()
    {
        requested_ = true;
        try
        {
            navigate(directory_.empty() ? std::filesystem::current_path() : directory_);
        }
        catch (const std::exception &error)
        {
            error_ = error.what();
        }
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
                {
                    if (!openHandler_)
                        throw std::runtime_error("ROM loading is unavailable.");
                    openHandler_(selection->path);
                    error_.clear();
                    open_ = false;
                    ImGui::CloseCurrentPopup();
                }
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
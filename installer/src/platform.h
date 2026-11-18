#pragma once
// OS calls setup needs; UTF-8 paths, u8path converts (Windows ANSI).
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace setup {

std::filesystem::path u8path(std::string_view s);
std::string u8string(const std::filesystem::path& p);

std::optional<std::string> env(const char* name);
std::filesystem::path home();
std::filesystem::path exe_dir();

// Is Grim Dawn.exe running (Linux /proc, Windows Toolhelp32); nullopt: unknown.
std::optional<bool> game_running();

// Windows only (else empty): Documents folder, registry strings.
std::filesystem::path documents_dir();
enum class Hive { kCurrentUser, kLocalMachine };
std::optional<std::string> registry_string(Hive hive, const std::string& key, const std::string& value);
std::vector<std::string> registry_subkeys(Hive hive, const std::string& key);

}  // namespace setup

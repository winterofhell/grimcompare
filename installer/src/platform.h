#pragma once
// The few OS calls setup needs. Paths are UTF-8 in std::string everywhere; u8path / u8string convert, because on
// Windows fs::path(std::string) reads the ANSI code page.
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

// Grim Dawn.exe running: Linux /proc (wine names the process after the exe; its cmdline starts with a Windows path),
// Windows Toolhelp32. nullopt: could not tell.
std::optional<bool> game_running();

// Windows only (empty elsewhere): the user's Documents folder, and registry strings.
std::filesystem::path documents_dir();
enum class Hive { kCurrentUser, kLocalMachine };
std::optional<std::string> registry_string(Hive hive, const std::string& key, const std::string& value);
std::vector<std::string> registry_subkeys(Hive hive, const std::string& key);

}  // namespace setup

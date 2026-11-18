#pragma once
// SHA-256 (FIPS 180-4), matching sha256sum hex as deploy.sh writes.
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace setup {

std::string sha256(std::span<const unsigned char> data);
std::optional<std::string> sha256_file(const std::filesystem::path& p);  // nullopt: unreadable

}  // namespace setup

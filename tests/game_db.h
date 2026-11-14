#pragma once
// The installed game's database for tests that need real records (null when the game is absent: the test
// skips). One instance for the whole test binary.
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "gdc/arz.h"

inline const gdc::arz::Database* game_db() {
  static const std::unique_ptr<gdc::arz::Database> db = []() -> std::unique_ptr<gdc::arz::Database> {
    std::filesystem::path dir;
    if (const char* d = std::getenv("GD_DIR")) dir = d;
    else if (const char* h = std::getenv("HOME")) dir = std::filesystem::path(h) / ".local/share/Steam/steamapps/common/Grim Dawn";
    if (dir.empty() || !std::filesystem::exists(dir / "database" / "database.arz")) return nullptr;
    return std::make_unique<gdc::arz::Database>(gdc::arz::Database::open_game(dir));
  }();
  return db.get();
}

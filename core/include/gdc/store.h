#pragma once
// One chain per segment per character: a full baseline, then RFC 7386 merge patches between keyed snapshots.
// Files: <root>/<character>/<segment>-<seq>-<full|delta>.json.gz; names sort in time order (docs/SNAPSHOT.md).
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/snapshot.h"

namespace gdc::store {

std::string gzip(std::string_view data);
std::string gunzip(std::string_view gz);

// Arrays keyed by identity so a patch touches only what changed. Keyed entries are in key order, so the engine's
// list order is lost. A missing or repeated key leaves the array as is.
std::string keyed(const snap::Snapshot& s);
snap::Snapshot from_keyed(std::string_view keyed_json);

std::string diff(std::string_view from, std::string_view to);
std::string apply(std::string_view target, std::string_view patch);

// A hash suffix keeps replaced names distinct.
std::string folder_name(std::string_view utf8_name);

enum Trigger : unsigned {
  kLoad = 1, kEquip = 2, kLevel = 4, kSkill = 8, kDevotion = 16, kAttribute = 32, kDifficulty = 64, kManual = 128
};
std::vector<std::string> tags(unsigned triggers);

// Files that are not record names are ignored.
struct Record {
  std::string segment;
  unsigned seq = 0;
  bool full = false;
  unsigned triggers = 0;
  snap::Snapshot snapshot;
};
void read(const std::filesystem::path& char_dir, const std::function<void(const Record&)>& each);

class Store {
 public:
  // Oldest finished segments are deleted past cap_bytes.
  Store(std::filesystem::path root, std::uint64_t cap_bytes);

  struct Written {
    std::filesystem::path file;
    bool full = false;
    std::uint64_t bytes = 0;
    std::vector<std::filesystem::path> deleted;
  };
  // On a throw the chain restarts with a full record.
  Written write(const snap::Snapshot& s, unsigned triggers);

 private:
  struct Chain {
    std::string segment;
    unsigned seq = 0;
    std::string prev;
    std::uint64_t bytes = 0;
  };
  std::vector<std::filesystem::path> rotate();

  std::filesystem::path root_;
  std::uint64_t cap_;
  std::map<std::string, Chain> chains_;
};

}  // namespace gdc::store

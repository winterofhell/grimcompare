#pragma once
// Auto-snapshot storage (docs/SNAPSHOT.md "Auto snapshots"). Per character folder, one chain per segment:
// a full baseline, then deltas. Each record is one gzip'd compact JSON file:
//   full:  {"format":"gdc-snap/1","kind":"full","segment":S,"seq":0,"trigger":[...],"taken_at":T,"snapshot":K}
//   delta: {"format":"gdc-snap/1","kind":"delta","segment":S,"seq":n,"trigger":[...],"taken_at":T,"patch":P}
// K is the snapshot JSON in keyed form (keyed()), P an RFC 7386 merge patch from record n-1's K to this one.
// Files: <root>/<character>/<segment>-<seq, 4 digits>-<full|delta>.json.gz; names sort in time order.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/snapshot.h"

namespace gdc::store {

// gzip (RFC 1952) with miniz deflate; gunzip throws std::runtime_error on malformed input or a bad CRC.
std::string gzip(std::string_view data);
std::string gunzip(std::string_view gz);

// Snapshot JSON with these top-level arrays turned into objects keyed by identity, so a patch touches only
// what changed: char_attributes, defense, damage_modifiers (by type), conversions ("from>to"), skills (by
// record), item_skill_modifiers ("skill_id:modifier_id"), equipped (by location), inventory (by id).
// Keyed entries are in key order (numeric if all keys are), so the engine's list order is not kept.
// An array whose key is missing or repeated stays an array. Compact text.
std::string keyed(const snap::Snapshot& s);
snap::Snapshot from_keyed(std::string_view keyed_json);  // objects back to arrays, in key order

// RFC 7386: diff(a, b) is the merge patch p with apply(a, p) == b (both JSON texts; "{}" if equal).
std::string diff(std::string_view from, std::string_view to);
std::string apply(std::string_view target, std::string_view patch);

// Folder name for a character: ASCII letters, digits, '-' and '_' kept, other bytes -> '_', at most 40;
// if anything was replaced, "-" and the name's FNV-1a 32 hash in hex follow. "" -> "unknown".
std::string folder_name(std::string_view utf8_name);

// Trigger bits; tags() names them in this order.
enum Trigger : unsigned {
  kLoad = 1, kEquip = 2, kLevel = 4, kSkill = 8, kDevotion = 16, kAttribute = 32, kDifficulty = 64, kManual = 128
};
std::vector<std::string> tags(unsigned triggers);

// Reading back (tools). Every record of one character folder, oldest first, rebuilt as a snapshot by
// applying each segment's patches to its full record. Throws std::runtime_error on a malformed record or
// a delta without its baseline; files that are not record names are ignored.
struct Record {
  std::string segment;
  unsigned seq = 0;
  bool full = false;
  unsigned triggers = 0;  // Trigger bits
  snap::Snapshot snapshot;
};
void read(const std::filesystem::path& char_dir, const std::function<void(const Record&)>& each);

class Store {
 public:
  // cap_bytes: total size of all record files under root; the oldest finished segments are deleted.
  Store(std::filesystem::path root, std::uint64_t cap_bytes);

  struct Written {
    std::filesystem::path file;
    bool full = false;
    std::uint64_t bytes = 0;
    std::vector<std::filesystem::path> deleted;  // rotated away by this write
  };
  // Throws on I/O errors (the chain is then restarted with a full record next time).
  Written write(const snap::Snapshot& s, unsigned triggers);

 private:
  struct Chain {
    std::string segment;
    unsigned seq = 0;
    std::string prev;  // keyed JSON of the last record
    std::uint64_t bytes = 0;
  };
  std::vector<std::filesystem::path> rotate();

  std::filesystem::path root_;
  std::uint64_t cap_;
  std::map<std::string, Chain> chains_;  // by folder name
};

}  // namespace gdc::store

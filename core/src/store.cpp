#include "gdc/store.h"

#include <miniz.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

namespace gdc::store {
namespace {

using J = nlohmann::ordered_json;
namespace fs = std::filesystem;

constexpr const char* kFormat = "gdc-snap/1";
constexpr std::uint32_t kMaxInflated = 256u << 20;  // one record; a full snapshot is ~1 MB of JSON

void put_u32(std::string& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

std::uint32_t get_u32(std::string_view s, std::size_t at) {
  std::uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(s[at + i]);
  return v;
}

struct Rule {
  const char* section;
  std::string (*key)(const J&);  // "" = no key
};

std::string num_or_str(const J& j, const char* field) {
  const auto it = j.find(field);
  if (it == j.end()) return {};
  if (it->is_number_integer()) return std::to_string(it->get<long long>());
  if (it->is_string()) return it->get<std::string>();
  return {};
}

const Rule kRules[] = {
    {"char_attributes", [](const J& e) { return num_or_str(e, "type"); }},
    {"defense", [](const J& e) { return num_or_str(e, "type"); }},
    {"damage_modifiers", [](const J& e) { return num_or_str(e, "type"); }},
    {"conversions",
     [](const J& e) {
       const auto f = num_or_str(e, "from"), t = num_or_str(e, "to");
       return f.empty() || t.empty() ? std::string{} : f + ">" + t;
     }},
    {"skills", [](const J& e) { return num_or_str(e, "record"); }},
    {"item_skill_modifiers",
     [](const J& e) {
       const auto a = num_or_str(e, "skill_id"), b = num_or_str(e, "modifier_id");
       return a.empty() || b.empty() ? std::string{} : a + ":" + b;
     }},
    {"item_skills", [](const J& e) { return num_or_str(e, "id"); }},  // two items can grant one record
    {"equipped", [](const J& e) { return num_or_str(e, "location"); }},
    {"inventory", [](const J& e) { return num_or_str(e, "id"); }},
};

bool numeric(const std::string& k) {
  return !k.empty() && std::all_of(k.begin(), k.end(), [](char c) { return (c >= '0' && c <= '9') || c == '-'; });
}

// Keyed sections in key order (numeric when every key is): a canonical order, so a rebuilt snapshot
// matches regardless of where a patch inserted a key.
J sorted_values(const J& obj) {
  std::vector<std::pair<std::string, const J*>> items;
  for (const auto& [k, v] : obj.items()) items.emplace_back(k, &v);
  const bool nums = std::all_of(items.begin(), items.end(), [](const auto& kv) { return numeric(kv.first); });
  std::stable_sort(items.begin(), items.end(), [&](const auto& a, const auto& b) {
    return nums ? std::stoll(a.first) < std::stoll(b.first) : a.first < b.first;
  });
  J arr = J::array();
  for (const auto& [k, v] : items) arr.push_back(*v);
  return arr;
}

J merge_diff(const J& a, const J& b) {
  if (!a.is_object() || !b.is_object()) return b;
  J p = J::object();
  for (const auto& [k, v] : a.items())
    if (!b.contains(k)) p[k] = nullptr;
  for (const auto& [k, v] : b.items()) {
    const auto it = a.find(k);
    if (it == a.end()) {
      p[k] = v;
    } else if (*it != v) {
      if (it->is_object() && v.is_object()) p[k] = merge_diff(*it, v);
      else p[k] = v;
    }
  }
  return p;
}

void merge_apply(J& target, const J& patch) {
  if (!patch.is_object()) {
    target = patch;
    return;
  }
  if (!target.is_object()) target = J::object();
  for (const auto& [k, v] : patch.items()) {
    if (v.is_null()) target.erase(k);
    else merge_apply(target[k], v);
  }
}

J parse(std::string_view text, const char* what) {
  try {
    return J::parse(text);
  } catch (const nlohmann::json::exception& e) {
    throw std::runtime_error(std::string(what) + ": " + e.what());
  }
}

std::string segment_of(const std::string& taken_at) {
  // "2026-11-14T12:03:19.056Z" -> "20261114-120319-056"
  std::string d;
  for (char c : taken_at)
    if (c >= '0' && c <= '9') d.push_back(c);
  d.resize(17, '0');  // taken_at missing or short: zeros
  return d.substr(0, 8) + "-" + d.substr(8, 6) + "-" + d.substr(14, 3);
}

void write_file(const fs::path& path, const std::string& bytes) {
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot create " + tmp.string());
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!f) throw std::runtime_error("cannot write " + tmp.string());
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(tmp, ec);
    throw std::runtime_error("cannot rename to " + path.string());
  }
}

}  // namespace

std::string gzip(std::string_view data) {
  std::size_t n = 0;
  void* raw = tdefl_compress_mem_to_heap(data.data(), data.size(), &n, TDEFL_DEFAULT_MAX_PROBES);
  if (!raw && !data.empty()) throw std::runtime_error("gzip: deflate failed");
  std::string out = {'\x1f', '\x8b', '\x08', '\0', '\0', '\0', '\0', '\0', '\0', '\xff'};  // no mtime, OS unknown
  out.append(static_cast<const char*>(raw), n);
  mz_free(raw);
  put_u32(out, static_cast<std::uint32_t>(
                   mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size())));
  put_u32(out, static_cast<std::uint32_t>(data.size()));
  return out;
}

std::string gunzip(std::string_view gz) {
  if (gz.size() < 18 || static_cast<unsigned char>(gz[0]) != 0x1f || static_cast<unsigned char>(gz[1]) != 0x8b ||
      gz[2] != 8)
    throw std::runtime_error("gunzip: not a gzip deflate stream");
  const unsigned flags = static_cast<unsigned char>(gz[3]);
  std::size_t p = 10;
  const std::size_t end = gz.size() - 8;
  auto need = [&](std::size_t n) {
    if (p + n > end) throw std::runtime_error("gunzip: truncated header");
  };
  if (flags & 4) {  // FEXTRA
    need(2);
    const std::size_t xlen = static_cast<unsigned char>(gz[p]) | (static_cast<unsigned char>(gz[p + 1]) << 8);
    p += 2;
    need(xlen);
    p += xlen;
  }
  for (unsigned bit : {8u, 16u})  // FNAME, FCOMMENT: zero-terminated
    if (flags & bit) {
      while (p < end && gz[p] != 0) ++p;
      need(1);
      ++p;
    }
  if (flags & 2) {  // FHCRC
    need(2);
    p += 2;
  }
  const std::uint32_t isize = get_u32(gz, end + 4);
  if (isize > kMaxInflated) throw std::runtime_error("gunzip: stated size too large");
  std::string out(isize, '\0');
  const std::size_t n = tinfl_decompress_mem_to_mem(out.data(), out.size(), gz.data() + p, end - p, 0);
  if (n == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || n != isize) throw std::runtime_error("gunzip: inflate failed");
  const auto crc = static_cast<std::uint32_t>(
      mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(out.data()), out.size()));
  if (crc != get_u32(gz, end)) throw std::runtime_error("gunzip: CRC mismatch");
  return out;
}

std::string keyed(const snap::Snapshot& s) {
  J j = parse(snap::to_json(s, -1), "keyed");
  for (const Rule& r : kRules) {
    const auto it = j.find(r.section);
    if (it == j.end() || !it->is_array()) continue;
    J obj = J::object();
    bool ok = true;
    for (const auto& e : *it) {
      const std::string k = r.key(e);
      if (k.empty() || obj.contains(k)) {
        ok = false;
        break;
      }
      obj[k] = e;
    }
    if (!ok) continue;
    J out = J::object();
    for (auto& e : sorted_values(obj)) {
      const std::string k = r.key(e);  // before the move: operator= takes its argument by value
      out[k] = std::move(e);
    }
    *it = std::move(out);
  }
  return j.dump();
}

snap::Snapshot from_keyed(std::string_view keyed_json) {
  J j = parse(keyed_json, "from_keyed");
  if (!j.is_object()) throw std::runtime_error("from_keyed: not an object");
  for (const Rule& r : kRules) {
    const auto it = j.find(r.section);
    if (it != j.end() && it->is_object()) *it = sorted_values(*it);
  }
  return snap::from_json(j.dump());
}

std::string diff(std::string_view from, std::string_view to) {
  return merge_diff(parse(from, "diff"), parse(to, "diff")).dump();
}

std::string apply(std::string_view target, std::string_view patch) {
  J t = parse(target, "apply");
  merge_apply(t, parse(patch, "apply"));
  return t.dump();
}

std::string folder_name(std::string_view name) {
  if (name.empty()) return "unknown";
  std::string out;
  bool replaced = false;
  for (char c : name) {
    const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    out.push_back(keep ? c : '_');
    replaced = replaced || !keep;
  }
  if (out.size() > 40) {
    out.resize(40);
    replaced = true;
  }
  if (replaced) {
    std::uint32_t h = 2166136261u;
    for (char c : name) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    char buf[12];
    std::snprintf(buf, sizeof buf, "-%08x", h);
    out += buf;
  }
  return out;
}

std::vector<std::string> tags(unsigned t) {
  static constexpr std::pair<unsigned, const char*> kNames[] = {
      {kLoad, "load"},         {kEquip, "equip"},         {kLevel, "level"},
      {kSkill, "skill"},       {kDevotion, "devotion"},   {kAttribute, "attribute"},
      {kDifficulty, "difficulty"}, {kManual, "manual"}};
  std::vector<std::string> out;
  for (const auto& [bit, name] : kNames)
    if (t & bit) out.emplace_back(name);
  return out;
}

void read(const fs::path& char_dir, const std::function<void(const Record&)>& each) {
  // <segment 19 chars>-<seq 4 digits>-<full|delta>.json.gz; name order = time order within a segment
  std::map<std::string, std::map<unsigned, std::pair<bool, fs::path>>> segs;
  for (const auto& f : fs::directory_iterator(char_dir)) {
    const std::string n = f.path().filename().string();
    const bool full = n.ends_with("-full.json.gz"), delta = n.ends_with("-delta.json.gz");
    if (!f.is_regular_file() || (!full && !delta) || n.size() < 25 || n[19] != '-' ||
        !std::all_of(n.begin() + 20, n.begin() + 24, [](char c) { return c >= '0' && c <= '9'; }))
      continue;
    segs[n.substr(0, 19)][static_cast<unsigned>(std::stoul(n.substr(20, 4)))] = {full, f.path()};
  }
  for (const auto& [segment, recs] : segs) {
    J state;  // keyed snapshot; null until the segment's full record
    for (const auto& [seq, fp] : recs) {
      const auto& [full, path] = fp;
      std::ifstream f(path, std::ios::binary);
      if (!f) throw std::runtime_error(path.string() + ": cannot open");
      const std::string gz{std::istreambuf_iterator<char>(f), {}};
      const J rec = parse(gunzip(gz), path.string().c_str());
      const char* body = full ? "snapshot" : "patch";
      if (!rec.is_object() || rec.value("format", "") != kFormat || !rec.contains(body))
        throw std::runtime_error(path.string() + ": not a " + kFormat + " record");
      if (full) state = rec[body];
      else if (state.is_null()) throw std::runtime_error(path.string() + ": delta without a baseline");
      else merge_apply(state, rec[body]);
      Record r;
      r.segment = segment;
      r.seq = seq;
      r.full = full;
      for (const auto& t : rec.value("trigger", J::array()))
        for (unsigned bit = 1; bit <= kManual; bit <<= 1)
          if (const auto n = tags(bit); !n.empty() && t == n[0]) r.triggers |= bit;
      r.snapshot = from_keyed(state.dump());
      each(r);
    }
  }
}

Store::Store(fs::path root, std::uint64_t cap_bytes) : root_(std::move(root)), cap_(cap_bytes) {}

Store::Written Store::write(const snap::Snapshot& s, unsigned triggers) {
  const std::string folder = folder_name(s.character_name);
  const fs::path dir = root_ / folder;
  fs::create_directories(dir);
  const std::string k = keyed(s);
  Chain& c = chains_[folder];
  // A new segment (full baseline) for the first record and once a chain holds a quarter of the cap, so
  // finished segments can be rotated away whole.
  const bool full = c.prev.empty() || c.bytes > cap_ / 4;
  if (full) c = Chain{segment_of(s.taken_at), 0, {}, 0};
  else ++c.seq;

  J rec{{"format", kFormat}, {"kind", full ? "full" : "delta"}, {"segment", c.segment}, {"seq", c.seq},
        {"trigger", tags(triggers)}, {"taken_at", s.taken_at}};
  if (full) rec["snapshot"] = J::parse(k);
  else rec["patch"] = merge_diff(J::parse(c.prev), J::parse(k));
  char seq[8];
  std::snprintf(seq, sizeof seq, "%04u", c.seq % 10000);
  const fs::path file = dir / (c.segment + "-" + seq + (full ? "-full" : "-delta") + ".json.gz");
  const std::string bytes = gzip(rec.dump());
  try {
    write_file(file, bytes);
  } catch (...) {
    c.prev.clear();  // restart with a full record
    throw;
  }
  c.prev = k;
  c.bytes += bytes.size();
  Written w{file, full, bytes.size(), {}};
  w.deleted = rotate();
  return w;
}

// Deletes the oldest whole segments (any character) until the total is within the cap. Active segments
// (the current chain of each character in this Store) are kept.
std::vector<fs::path> Store::rotate() {
  struct Seg {
    std::vector<fs::path> files;
    std::uint64_t bytes = 0;
  };
  std::map<std::string, Seg> segs;  // "segment\x01folder", oldest first
  std::uint64_t total = 0;
  std::error_code ec;
  for (const auto& d : fs::directory_iterator(root_, ec)) {
    if (!d.is_directory()) continue;
    for (const auto& f : fs::directory_iterator(d.path(), ec)) {
      const std::string name = f.path().filename().string();
      if (!f.is_regular_file() || !name.ends_with(".json.gz") || name.size() < 19) continue;
      const std::uint64_t n = f.file_size(ec);
      Seg& s = segs[name.substr(0, 19) + "\x01" + d.path().filename().string()];
      s.files.push_back(f.path());
      s.bytes += n;
      total += n;
    }
  }
  std::set<std::string> active;
  for (const auto& [folder, c] : chains_) active.insert(c.segment + "\x01" + folder);
  std::vector<fs::path> deleted;
  for (auto& [key, s] : segs) {
    if (total <= cap_) break;
    if (active.contains(key)) continue;
    for (const auto& f : s.files)
      if (fs::remove(f, ec)) deleted.push_back(f);
    total -= s.bytes;
  }
  return deleted;
}

}  // namespace gdc::store

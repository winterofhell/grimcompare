#include "gdc/pe_exports.h"

#include <cstring>
#include <optional>

namespace gdc::pe {
namespace {

class Reader {
 public:
  Reader(std::span<const std::byte> img, Layout layout) : img_(img), layout_(layout) {}

  template <class T>
  std::optional<T> at(std::uint64_t off) const {
    if (off > img_.size() || img_.size() - off < sizeof(T)) return std::nullopt;
    T v;
    std::memcpy(&v, img_.data() + off, sizeof(T));
    return v;
  }

  std::optional<std::string> cstr(std::uint64_t off) const {
    if (off >= img_.size()) return std::nullopt;
    const auto* p = reinterpret_cast<const char*>(img_.data() + off);
    const std::size_t max = img_.size() - off;
    const void* nul = std::memchr(p, 0, max);
    if (!nul) return std::nullopt;
    return std::string(p, static_cast<const char*>(nul) - p);
  }

  bool load_sections(std::uint64_t first, std::uint16_t count) {
    for (std::uint16_t i = 0; i < count; ++i) {
      const std::uint64_t h = first + std::uint64_t{i} * 40;
      auto va = at<std::uint32_t>(h + 12), raw_size = at<std::uint32_t>(h + 16),
           raw_ptr = at<std::uint32_t>(h + 20);
      if (!va || !raw_size || !raw_ptr) return false;
      sections_.push_back({*va, *raw_size, *raw_ptr});
    }
    return true;
  }

  std::optional<std::uint64_t> rva_to_off(std::uint32_t rva) const {
    if (layout_ == Layout::Mapped) return rva;
    for (const auto& s : sections_) {
      if (rva >= s.va && rva - s.va < s.raw_size) return std::uint64_t{s.raw_ptr} + (rva - s.va);
    }
    return std::nullopt;
  }

  template <class T>
  std::optional<T> at_rva(std::uint32_t rva) const {
    auto off = rva_to_off(rva);
    if (!off) return std::nullopt;
    return at<T>(*off);
  }

 private:
  struct Section {
    std::uint32_t va, raw_size, raw_ptr;
  };
  std::span<const std::byte> img_;
  Layout layout_;
  std::vector<Section> sections_;
};

}  // namespace

ExportTable read_exports(std::span<const std::byte> image, Layout layout) {
  ExportTable out;
  auto fail = [&](const char* why) {
    out.exports.clear();
    out.error = why;
    return out;
  };
  Reader r(image, layout);

  if (r.at<std::uint16_t>(0) != std::uint16_t{0x5A4D}) return fail("no MZ header");
  const auto lfanew = r.at<std::uint32_t>(0x3C);
  if (!lfanew || r.at<std::uint32_t>(*lfanew) != std::uint32_t{0x00004550}) return fail("no PE signature");
  const std::uint64_t file_hdr = std::uint64_t{*lfanew} + 4;
  const auto nsect = r.at<std::uint16_t>(file_hdr + 2);
  const auto opt_size = r.at<std::uint16_t>(file_hdr + 16);
  const std::uint64_t opt = file_hdr + 20;
  if (!nsect || !opt_size) return fail("truncated file header");
  if (r.at<std::uint16_t>(opt) != std::uint16_t{0x20B}) return fail("not PE32+");
  const auto ndirs = r.at<std::uint32_t>(opt + 108);
  if (!ndirs || *ndirs < 1 || *opt_size < 112 + 8) return fail("no data directories");
  const auto exp_rva = r.at<std::uint32_t>(opt + 112), exp_size = r.at<std::uint32_t>(opt + 116);
  if (!exp_rva || !exp_size) return fail("truncated optional header");
  if (*exp_rva == 0 || *exp_size == 0) return out;
  if (!r.load_sections(opt + *opt_size, *nsect)) return fail("truncated section table");

  const auto nfuncs = r.at_rva<std::uint32_t>(*exp_rva + 20);
  const auto nnames = r.at_rva<std::uint32_t>(*exp_rva + 24);
  const auto funcs = r.at_rva<std::uint32_t>(*exp_rva + 28);
  const auto names = r.at_rva<std::uint32_t>(*exp_rva + 32);
  const auto ords = r.at_rva<std::uint32_t>(*exp_rva + 36);
  if (!nfuncs || !nnames || !funcs || !names || !ords) return fail("truncated export directory");
  if (*nnames > image.size() / 4 || *nfuncs > image.size() / 4) return fail("export counts exceed image");

  out.exports.reserve(*nnames);
  for (std::uint32_t i = 0; i < *nnames; ++i) {
    const auto name_rva = r.at_rva<std::uint32_t>(*names + i * 4);
    const auto ord = r.at_rva<std::uint16_t>(*ords + i * 2);
    if (!name_rva || !ord) return fail("truncated name/ordinal table");
    if (*ord >= *nfuncs) return fail("ordinal out of range");
    const auto fn_rva = r.at_rva<std::uint32_t>(*funcs + std::uint32_t{*ord} * 4);
    const auto name_off = r.rva_to_off(*name_rva);
    if (!fn_rva || !name_off) return fail("truncated function table");
    auto name = r.cstr(*name_off);
    if (!name) return fail("unterminated export name");
    const bool fwd = *fn_rva >= *exp_rva && *fn_rva - *exp_rva < *exp_size;
    out.exports.push_back({std::move(*name), *fn_rva, fwd});
  }
  return out;
}

}  // namespace gdc::pe

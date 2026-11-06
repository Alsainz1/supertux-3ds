#include "packed_assets.hpp"
#include "platform.hpp"
#include <cstring>
#include <stdexcept>
#include <zlib.h>

namespace {
template<class T> T get(PHYSFS_File* file) {
  T value{};
  if (PHYSFS_readBytes(file, &value, sizeof(value)) != sizeof(value))
    throw std::runtime_error("truncated cache index");
  return value;
}
}
void PackedAssets::open() {
  if (attempted) return;
  attempted = true;
  auto* metadata = PHYSFS_openRead((name + ".idx").c_str());
  if (!metadata) return;
  PHYSFS_setBuffer(metadata, 64*1024);
  try {
    char magic[8];
    if (PHYSFS_readBytes(metadata, magic, 8) != 8 || std::memcmp(magic, "STPACK01", 8))
      throw std::runtime_error("unsupported cache version");
    const auto count = get<uint32_t>(metadata);
    if (count > 100000) throw std::runtime_error("invalid cache count");
    for (uint32_t i = 0; i < count; ++i) {
      Entry entry;
      entry.offset = get<uint64_t>(metadata);
      entry.packed = get<uint32_t>(metadata);
      entry.raw = get<uint32_t>(metadata);
      const auto length = get<uint16_t>(metadata);
      if (!length || length > 1024 || !entry.raw || entry.raw > 64*1024*1024 || entry.packed > entry.raw)
        throw std::runtime_error("invalid cache entry");
      std::string key(length, '\0');
      if (PHYSFS_readBytes(metadata, key.data(), length) != length)
        throw std::runtime_error("truncated cache key");
      index.emplace(std::move(key), entry);
    }
    file = PHYSFS_openRead((name + ".bin").c_str());
    if (!file) throw std::runtime_error("missing cache data");
    const auto size = PHYSFS_fileLength(file);
    for (const auto& item : index)
      if (item.second.offset > static_cast<uint64_t>(size) ||
          item.second.packed > static_cast<uint64_t>(size)-item.second.offset)
        throw std::runtime_error("cache entry outside data file");
    ctr::log("[Cache] %s: %lu entries\n", name.c_str(), static_cast<unsigned long>(index.size()));
  } catch (const std::exception& e) {
    ctr::log("[Cache] %s: %s; using originals\n", name.c_str(), e.what());
    close();
  }
  PHYSFS_close(metadata);
}
const std::map<std::string, PackedAssets::Entry>& PackedAssets::entries() { open(); return index; }
bool PackedAssets::read(const std::string& filename, std::vector<unsigned char>& bytes) {
  open();
  const std::string key = filename.empty() || filename.front() == '/' ? filename : '/' + filename;
  const auto found = index.find(key);
  if (!file || found == index.end()) return false;
  const auto& entry = found->second;
  if (!PHYSFS_seek(file, entry.offset)) return false;
  bytes.resize(entry.raw);
  if (entry.packed == entry.raw)
    return PHYSFS_readBytes(file, bytes.data(), entry.raw) == entry.raw;
  std::vector<unsigned char> compressed(entry.packed);
  if (PHYSFS_readBytes(file, compressed.data(), entry.packed) != entry.packed) return false;
  uLongf size = entry.raw;
  return uncompress(bytes.data(), &size, compressed.data(), entry.packed) == Z_OK && size == entry.raw;
}
void PackedAssets::close() {
  if (file) PHYSFS_close(file);
  file = nullptr; index.clear();
}

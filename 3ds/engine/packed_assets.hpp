#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <physfs.h>

// Immutable indexed packs: one open handle instead of thousands of SD opens.
// Both packs are optional; callers retain the original asset path as fallback.
class PackedAssets {
public:
  struct Entry { uint64_t offset; uint32_t packed, raw; };
  explicit PackedAssets(const char* name) : name(name) {}
  bool read(const std::string& key, std::vector<unsigned char>& bytes);
  const std::map<std::string, Entry>& entries();
  void close();
private:
  void open();
  std::string name;
  bool attempted = false;
  PHYSFS_File* file = nullptr;
  std::map<std::string, Entry> index;
};

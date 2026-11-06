#pragma once
#include <string>
struct LevelProbe {
  std::string name;
  unsigned sectors = 0;
  unsigned tilemaps = 0;
  unsigned tiles = 0;
};
LevelProbe probe_level(const std::string& path);

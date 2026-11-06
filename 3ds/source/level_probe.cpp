#include "level_probe.hpp"
#include <sexp/parser.hpp>
#include <fstream>
#include <stdexcept>

namespace {
void visit(const sexp::Value& node, LevelProbe& result) {
  if (!node.is_array()) return;
  const auto& values = node.as_array();
  if (values.empty()) return;
  if (values[0].is_symbol()) {
    const auto& tag = values[0].as_string();
    if (tag == "sector") ++result.sectors;
    if (tag == "tilemap") ++result.tilemaps;
    if (tag == "tiles") result.tiles += static_cast<unsigned>(values.size() - 1);
  }
  for (const auto& child : values) visit(child, result);
}
}

LevelProbe probe_level(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot open level: " + path);
  // Identical parser and array mode to upstream ReaderDocument::from_stream.
  auto root = sexp::Parser::from_stream(input, sexp::Parser::USE_ARRAYS);
  if (!root.is_array() || root.as_array().empty() ||
      !root.as_array()[0].is_symbol() || root.as_array()[0].as_string() != "supertux-level")
    throw std::runtime_error("Not a SuperTux level");
  LevelProbe result;
  for (const auto& field : root.as_array()) {
    if (!field.is_array()) continue;
    const auto& v = field.as_array();
    if (v.size() < 2 || !v[0].is_symbol()) continue;
    if (v[0].as_string() == "version" && v[1].as_int() != 3)
      throw std::runtime_error("Unsupported level format");
    if (v[0].as_string() == "name") {
      if (v[1].is_string()) result.name = v[1].as_string();
      else if (v[1].is_array() && v[1].as_array().size() == 2)
        result.name = v[1].as_array()[1].as_string();
    }
  }
  visit(root, result);
  if (!result.sectors || !result.tilemaps || !result.tiles)
    throw std::runtime_error("Level has no sector/tile data");
  return result;
}

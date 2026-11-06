// Native 3DS texture loading: retain metadata, decode only at first draw.
#include "video/sdl/sdl_texture.hpp"
#include "video/texture_manager.hpp"
#include "video/sdl_surface.hpp"
#include <physfs.h>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <sstream>
#include "physfs/ifile_stream.hpp"
#include "performance.hpp"
#include "gpu_renderer.hpp"

namespace {
std::string compatible_image(const std::string& filename) {
  // Old goal tiles in the upstream tileset still name the pre-move decals.
  if (filename=="/images/decal/snow/exitbg.png" || filename=="images/decal/snow/exitbg.png") return "/images/tiles/goal/deprecated/exitbg.png";
  if (filename=="/images/decal/snow/exitfg.png" || filename=="images/decal/snow/exitfg.png") return "/images/tiles/goal/deprecated/exitfg.png";
  return filename;
}
Size image_size(const std::string& filename) {
  // One small index replaces thousands of SD file-open/header-read operations.
  static const auto dimensions = [] {
    std::unordered_map<std::string, Size> index;
    if (!PHYSFS_exists("3ds-image-dimensions.txt")) return index;
    IFileStream input("3ds-image-dimensions.txt");
    std::string line;
    while (std::getline(input, line)) {
      std::istringstream record(line);
      int width, height;
      if (!(record >> width >> height) || width <= 0 || height <= 0) continue;
      std::string path;
      std::getline(record >> std::ws, path);
      if (!path.empty()) index.emplace(path, Size(width, height));
    }
    return index;
  }();
  if (const auto found = dimensions.find(filename); found != dimensions.end()) return found->second;
  PHYSFS_File* file = PHYSFS_openRead(filename.c_str());
  if (!file) throw std::runtime_error("Missing image: " + filename);
  unsigned char header[24]{};
  const auto count = PHYSFS_readBytes(file, header, sizeof(header));
  PHYSFS_close(file);
  if (count == 24 && std::memcmp(header, "\x89PNG\r\n\x1a\n", 8) == 0) {
    auto be32 = [](const unsigned char* p) -> unsigned {
      return (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16) | (unsigned(p[2]) << 8) | p[3];
    };
    const unsigned width = be32(header + 16), height = be32(header + 20);
    if (!width || !height || width > 16384 || height > 16384)
      throw std::runtime_error("Invalid PNG dimensions: " + filename);
    return Size(static_cast<int>(width), static_cast<int>(height));
  }
  // Rare non-PNG artwork: inspect through SDL_image, then release the pixels.
  const auto surface = SDLSurface::from_file(filename);
  return Size(surface->w, surface->h);
}
}

SDLTexture::SDLTexture(const std::string& filename, const std::optional<Rect>& rect,
                       const Sampler& sampler) :
  Texture(sampler), m_texture(nullptr), m_width(0), m_height(0),
  m_lazy_filename(compatible_image(filename)), m_lazy_rect(rect)
{
  const auto size = rect ? Size(rect->get_width(), rect->get_height()) : image_size(m_lazy_filename);
  m_width = size.width;
  m_height = size.height;
}

const ctr_gpu::Texture& SDLTexture::get_gpu_texture() const {
  if (!m_gpu_texture && !m_lazy_filename.empty()) {
    ctr_perf::Scope timer(ctr_perf::Texture);
    auto* self = const_cast<SDLTexture*>(this);
    self->m_gpu_texture = ctr_gpu::load_cached(m_lazy_filename, m_lazy_rect);
    if (!self->m_gpu_texture) {
      auto surface = m_lazy_rect ?
        TextureManager::current()->create_image_surface_raw(m_lazy_filename, *m_lazy_rect, m_sampler) :
        SDLSurface::from_file(m_lazy_filename);
      self->reload(*surface);
    }
    if ((m_lazy_filename.find("images/background/")!=std::string::npos || m_lazy_filename.find("images/particles/cloud.png")!=std::string::npos))
      ctr_gpu::smooth_background(*self->m_gpu_texture);
  }
  if (!m_gpu_texture) throw std::runtime_error("GPU texture is not initialized");
  return *m_gpu_texture;
}

SDL_Texture* SDLTexture::get_texture() const { return m_texture; }

void SDLTexture::prefetch_cached() {
  if (!m_gpu_texture && !m_lazy_filename.empty())
    m_gpu_texture=ctr_gpu::load_cached(m_lazy_filename, m_lazy_rect);
}

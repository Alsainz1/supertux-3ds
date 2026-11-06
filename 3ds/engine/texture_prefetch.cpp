#include "texture_prefetch.hpp"
#include "video/sdl/sdl_texture.hpp"
#include <deque>
#include <unordered_set>
namespace ctr_prefetch {
namespace {
struct Entry { std::weak_ptr<Texture> texture; const Texture* id; };
std::deque<Entry> pending;
std::unordered_set<const Texture*> queued;
}
void enqueue(const std::shared_ptr<Texture>& texture, bool urgent) {
  const auto& native=static_cast<const SDLTexture&>(*texture);
  // Limit speculative work to small terrain images and keep ownership weak.
  if (native.gpu_ready() || native.get_image_width()*native.get_image_height()>128*128 ||
      queued.count(texture.get())) return;
  if (pending.size()>=64) {
    if (!urgent) return;
    queued.erase(pending.back().id);pending.pop_back();
  }
  queued.insert(texture.get());
  if (urgent) pending.push_front({texture, texture.get()});
  else pending.push_back({texture, texture.get()});
}
void pump() {
  for (int inspected=0; inspected<4 && !pending.empty(); ++inspected) {
    const auto entry=pending.front(); pending.pop_front(); queued.erase(entry.id);
    const auto texture=entry.texture.lock();
    if (!texture) continue;
    auto& native=static_cast<SDLTexture&>(*texture);
    if (native.gpu_ready()) continue;
    // At most one cache read/upload per submitted frame. Never invoke a PNG
    // decoder here when an asset is absent from the prepared cache.
    native.prefetch_cached();
    break;
  }
}
void clear() { pending.clear(); queued.clear(); }
}

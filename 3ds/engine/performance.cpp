#include "performance.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cstdio>
namespace ctr_perf {
namespace {
Uint64 totals[Count]{}, maxima[Count]{};
unsigned frames = 0, loads = 0;
Uint32 last_report = 0;
}
bool enabled() {
  static const bool active=[] {
    auto* marker=std::fopen("sdmc:/3ds/supertux070/profile.flag","r");
    if (!marker) return false;
    std::fclose(marker); return true;
  }();
  return active;
}
void add(Phase phase, Uint64 ticks) {
  totals[phase] += ticks;
  maxima[phase] = std::max(maxima[phase], ticks);
  if (phase == Texture) ++loads;
}
void frame() {
  if (!enabled()) return;
  ++frames;
  const Uint32 now = SDL_GetTicks();
  if (!last_report) last_report = now;
  if (now - last_report < 5000) return;
  const double ms = 1000.0 / SDL_GetPerformanceFrequency();
  const char* names[] = {"logic", "draw", "wait", "texture", "audio"};
  for (int i = 0; i < Count; ++i) {
    ctr::log("[Perf] %s avg %.2f max %.2f ms\n", names[i], totals[i]*ms/frames, maxima[i]*ms);
    totals[i] = maxima[i] = 0;
  }
  ctr::log("[Perf] %u texture loads / %u frames\n", loads, frames);
  frames = loads = 0; last_report = now;
}
}

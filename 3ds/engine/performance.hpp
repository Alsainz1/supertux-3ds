#pragma once
#include <SDL_timer.h>
namespace ctr_perf {
enum Phase { Logic, Draw, Wait, Texture, Audio, Count };
bool enabled();
void add(Phase phase, Uint64 ticks);
void frame();
class Scope {
  Phase phase;
  Uint64 start;
public:
  explicit Scope(Phase p) : phase(p), start(enabled() ? SDL_GetPerformanceCounter() : 0) {}
  ~Scope() { if (start) add(phase, SDL_GetPerformanceCounter() - start); }
};
}

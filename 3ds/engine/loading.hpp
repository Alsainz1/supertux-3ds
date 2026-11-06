#pragma once
namespace ctr_loading {
void protect_startup();void shutdown();
bool active();void begin_audio();void end_audio();void frame_presented();
class Scope {public:Scope();~Scope();Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;};
}

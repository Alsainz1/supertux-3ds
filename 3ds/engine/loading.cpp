#include "loading.hpp"
#include "gpu_renderer.hpp"
#include "native_hud.hpp"
extern "C" {
#include <3ds/types.h>
#include <3ds/services/apt.h>
}
#include <atomic>
namespace ctr_loading {
namespace {
std::atomic<unsigned> depth{0};
std::atomic<bool> waiting_for_frame{false};
bool sleep_guard=false, previous_sleep=true;
void protect_sleep(){
  if(sleep_guard)return;
  previous_sleep=aptIsSleepAllowed();
  aptSetSleepAllowed(false);sleep_guard=true;
}
void restore_sleep(){
  if(!sleep_guard)return;
  sleep_guard=false;aptSetSleepAllowed(previous_sleep);
}
}
bool active(){return depth.load(std::memory_order_acquire)!=0 || waiting_for_frame.load(std::memory_order_acquire);}
void protect_startup(){protect_sleep();}
void shutdown(){waiting_for_frame=false;restore_sleep();}
Scope::Scope(){
  if(depth.fetch_add(1)==0){
    protect_sleep();
    if(!waiting_for_frame.load())begin_audio();
    ctr_gpu::show_loading();
  }
}
Scope::~Scope(){if(depth.fetch_sub(1)==1){waiting_for_frame=true;ctr_hud::invalidate();}}
void frame_presented(){
  if(depth.load()!=0 || !waiting_for_frame.load())return;
  // Re-enabling sleep can immediately process a closed lid. Finish GPU work
  // first, then restore audio and the prior sleep policy at this safe boundary.
  ctr_gpu::wait_idle();
  end_audio();waiting_for_frame=false;restore_sleep();
}
}

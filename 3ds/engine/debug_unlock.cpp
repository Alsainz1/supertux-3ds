#include "debug_unlock.hpp"
extern "C" {
#include <3ds/types.h>
#include <3ds/services/hid.h>
}
#include <physfs.h>
#include <array>
#include <chrono>
#include "gui/menu_manager.hpp"
#include "supertux/game_session.hpp"
#include "supertux/profile_manager.hpp"
#include "supertux/screen_manager.hpp"
#include "supertux/resources.hpp"
#include "video/drawing_context.hpp"
#include "platform.hpp"
namespace ctr_unlock {
namespace {
int profile_id=-1;bool enabled=false;
using Clock=std::chrono::steady_clock;
Clock::time_point last_input{},confirmed{};
std::string marker(){return ProfileManager::current()->get_current_profile().get_basedir()+"/debug-all-levels.unlock";}
}
bool active(){
  const int id=ProfileManager::current()->get_current_profile().get_id();
  if(id!=profile_id){profile_id=id;enabled=PHYSFS_exists(marker().c_str())!=0;}
  return enabled;
}
bool notice(){return confirmed!=Clock::time_point{} && Clock::now()-confirmed<std::chrono::seconds(8);}
bool update_credits(){
  static unsigned index=0;static u32 held_before=0;
  const u32 held=hidKeysHeld();const u32 down=held&~held_before;held_before=held;
  const auto& screens=ScreenManager::current()->get_screen_stack();
  auto* game=screens.empty()?nullptr:dynamic_cast<GameSession*>(screens.back().get());
  if(!game || game->get_level_file().find("/misc/credits.stl")==std::string::npos || MenuManager::instance().is_active()){index=0;return false;}
  constexpr std::array<u32,10> sequence{KEY_DUP,KEY_DUP,KEY_DDOWN,KEY_DDOWN,KEY_DLEFT,KEY_DRIGHT,KEY_DLEFT,KEY_DRIGHT,KEY_B,KEY_A};
  const u32 key=down&(KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT|KEY_A|KEY_B|KEY_X|KEY_Y|KEY_START|KEY_SELECT|KEY_L|KEY_R);
  if(!key)return index==9 && (held&KEY_B);
  if(Clock::now()-last_input>std::chrono::seconds(10))index=0;
  last_input=Clock::now();
  const bool consume=index>=8 && key==sequence[index];
  index=key==sequence[index]?index+1:(key==KEY_DUP?1:0);
  if(index==sequence.size()){
    index=0;
    auto& profile=ProfileManager::current()->get_current_profile();profile.create_basedir();
    auto* file=PHYSFS_openWrite(marker().c_str());
    if(file){const char value[]="3DS debug: unlock all campaign levels/worlds\n";const bool ok=PHYSFS_writeBytes(file,value,sizeof(value)-1)==sizeof(value)-1;const bool closed=PHYSFS_close(file)!=0;
      if(ok && closed){profile_id=profile.get_id();enabled=true;confirmed=Clock::now();ctr::log("[Debug] All campaign levels/worlds unlocked for profile %d\n",profile_id);}}
  }
  return consume;
}
void draw_notice(DrawingContext& c){if(!notice())return;
  c.color().draw_filled_rect(Rectf(12,20,c.get_width()-12,90),Color(0,0,0,.9f),8,LAYER_GUI+10);
  c.color().draw_text(Resources::normal_font,"DEBUG: All levels and worlds unlocked",Vector(c.get_width()/2,40),ALIGN_CENTER,LAYER_GUI+11,Color(1,1,.4f));
}
}

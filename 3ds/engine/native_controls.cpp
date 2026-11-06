#include "native_controls.hpp"
#include "gui/menu_manager.hpp"
#include "supertux/menu/options_menu.hpp"
#include "native_hud.hpp"
#include "supertux/profile_manager.hpp"
#include "supertux/resources.hpp"
#include "control/input_manager.hpp"
#include "video/drawing_context.hpp"
#include <physfs.h>
namespace ctr_controls {
namespace {int last_profile=-1;bool swap_buttons=false;
std::string file(){return ProfileManager::current()->get_current_profile().get_basedir()+"/swap-jump-run.cfg";}}
bool visible(){auto* options=dynamic_cast<OptionsMenu*>(MenuManager::instance().current_menu());return options && options->native_controls_page();}
bool swapped(){
  if(!ProfileManager::current())return false;
  const int id=ProfileManager::current()->get_current_profile().get_id();
  if(id!=last_profile){last_profile=id;swap_buttons=false;if(auto* f=PHYSFS_openRead(file().c_str())){char value='0';PHYSFS_readBytes(f,&value,1);PHYSFS_close(f);swap_buttons=value=='1';}}
  return swap_buttons;
}
void toggle(){const bool next=!swapped();auto& p=ProfileManager::current()->get_current_profile();p.create_basedir();
  if(auto* f=PHYSFS_openWrite(file().c_str())){const char value=next?'1':'0';const bool ok=PHYSFS_writeBytes(f,&value,1)==1;const bool closed=PHYSFS_close(f)!=0;if(ok&&closed)swap_buttons=next;}
  InputManager::current()->get_controller().reset();
}
bool touch(int x,int y){if(!visible())return false;if(x<16||x>304||y<62||y>112)return false;toggle();return true;}
void draw(DrawingContext& c){
  if(!visible())return;
  c.color().draw_filled_rect(Rectf(32,124,608,224),Color(0,0,0,.85f),10,LAYER_GUI+5);
  c.color().draw_text(ctr_hud::menu_font(),swapped()?"Swap jump/run: ON":"Swap jump/run: OFF",Vector(320,142),ALIGN_CENTER,LAYER_GUI+6);
  c.color().draw_text(ctr_hud::menu_font(),swapped()?"Y: jump    B: run":"B: jump    Y: run",Vector(320,184),ALIGN_CENTER,LAYER_GUI+6,Color(1,1,.5f));
}
}

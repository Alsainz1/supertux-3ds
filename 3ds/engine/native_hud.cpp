// Native two-screen level HUD, using existing game art and real save/stat values.
#include "native_hud.hpp"
#include "debug_unlock.hpp"
#include "native_controls.hpp"
#include "platform.hpp"
extern "C" {
#include <3ds/types.h>
#include <3ds/services/hid.h>
}
#include "gui/menu_manager.hpp"
#include "gui/menu.hpp"
#include "supertux/title_screen.hpp"
#include "object/background.hpp"
#include "object/text_object.hpp"
#include "object/display_effect.hpp"
#include "supertux/screen_fade.hpp"
#include "worldmap/worldmap.hpp"
#include "worldmap/worldmap_sector.hpp"
#include "worldmap/level_tile.hpp"
#include "video/sdl/sdl_video_system.hpp"
#include <chrono>
#include <cmath>
#include "object/gradient.hpp"
#include "object/camera.hpp"
#include "object/level_time.hpp"
#include "object/player.hpp"
#include "sprite/sprite.hpp"
#include "sprite/sprite_manager.hpp"
#include "supertux/game_session.hpp"
#include "supertux/level.hpp"
#include "supertux/savegame.hpp"
#include "supertux/player_status.hpp"
#include "supertux/resources.hpp"
#include "supertux/screen_manager.hpp"
#include "supertux/sector.hpp"
#include "video/bitmap_font.hpp"
#include "video/ttf_font.hpp"
#include "video/ttf_surface_manager.hpp"
#include <sstream>
#include "video/compositor.hpp"
#include "video/drawing_context.hpp"
#include "video/canvas.hpp"
#include "video/surface.hpp"
#include "video/video_system.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <algorithm>
#include <cstdio>
namespace ctr_hud {
namespace {
struct Assets {
  FontPtr menu=std::make_shared<TTFFont>("fonts/SuperTux-Medium.ttf",28,1.f,2,1);
  SurfacePtr frame=Surface::from_file("images/engine/menu/frame.png");
  FontPtr text=std::make_shared<TTFFont>("fonts/SuperTux-Medium.ttf",13,1.f,1,0);
  FontPtr title_font=std::make_shared<TTFFont>("fonts/SuperTux-Medium.ttf",14,1.f,1,0);
  FontPtr author_font=std::make_shared<TTFFont>("fonts/SuperTux-Medium.ttf",10,1.f,1,0);

  SurfacePtr coin=Surface::from_file("images/engine/hud/coins-0.png");
  SurfacePtr plain_coin=Surface::from_file("images/objects/coin/coin-0.png");
  SurfacePtr time=Surface::from_file("images/engine/hud/time-0.png");
  SurfacePtr doll=Surface::from_file("images/powerups/1up/1up.png");
  SurfacePtr secret=Surface::from_file("images/engine/hud/secret-icon.png");
  SurfacePtr ring=Surface::from_file("images/engine/hud/item_pocket.png");
  std::map<BonusType,SpritePtr> powerups;
  std::string preview_key;
  std::vector<std::pair<int,SpritePtr>> backgrounds;
  Assets(){for(auto type:{BONUS_GROWUP,BONUS_FIRE,BONUS_ICE,BONUS_AIR,BONUS_EARTH})
    powerups[type]=SpriteManager::current()->create(PlayerStatus::get_bonus_sprite(type));}
};
std::unique_ptr<Assets> assets;
bool dirty=true;
ScreenFade* frame_fade=nullptr;
std::vector<DisplayEffect*> frame_effects;
bool rendering_effects=false;
const Camera* shake_camera=nullptr;
Vector shake_delta,last_shake;
Vector reserve_center(160,124);
float reserve_radius=50;
struct BossHead {SurfacePtr icon;int lives,max_lives;};
std::vector<BossHead> boss_heads;
unsigned repaint_left=0;
const Menu* last_menu=nullptr;std::size_t last_help=0;
std::vector<std::pair<const DisplayEffect*,float>> last_effect_states;
std::vector<std::pair<const TextObject*,std::size_t>> last_text_states;
const void* last_bottom_screen=nullptr;
bool last_skip=false;
bool had_effects=false;
bool had_notice=false;
std::vector<TextObject*> subtitles;
bool rendering_text=false;
bool map_bottom=false;
bool last_overlay=false;
int previous_pocket=BONUS_NONE;
int credited_level_dolls=0;
std::chrono::steady_clock::time_point bounce_start{};
float bounce_time(){return std::chrono::duration<float>(std::chrono::steady_clock::now()-bounce_start).count();}
bool menu_active(){auto& m=MenuManager::instance();return m.is_active()||m.has_dialog();}
std::string last_file;
const Sector* last_sector=nullptr;
std::array<int,7> last_values{};
Assets& art(){if(!assets)assets=std::make_unique<Assets>();return *assets;}
worldmap::WorldMap* map_screen(){const auto& stack=ScreenManager::current()->get_screen_stack();return stack.empty()?nullptr:dynamic_cast<worldmap::WorldMap*>(stack.back().get());}
GameSession* game(){const auto& stack=ScreenManager::current()->get_screen_stack();return stack.empty()?nullptr:dynamic_cast<GameSession*>(stack.back().get());}
void image(DrawingContext& c,const SurfacePtr& s,float x,float y,float w,float h,int layer=10){
  c.color().draw_surface_scaled(s,Rectf(x,y,x+w,y+h),layer);
}
void label(DrawingContext& c,const std::string& s,float x,float y,Color color=Color::WHITE,bool center=false){
  c.color().draw_text(art().text,s,Vector(x,y),center?ALIGN_CENTER:ALIGN_LEFT,20,color);
}

void draw_preview(DrawingContext& c,GameSession& g,Assets& a){
  auto& sector=g.get_current_sector();BIND_SECTOR(sector);
  c.color().draw_filled_rect(Rectf(0,0,320,240),Color(.2f,.35f,.5f),-10000);
  for(auto& gradient:sector.get_objects_by_type<Gradient>())gradient.draw(c);
  std::string key=g.get_level_file();
  for(auto& background:sector.get_objects_by_type<Background>())key+='|'+background.get_image();
  if(key!=a.preview_key){
    a.backgrounds.clear();a.preview_key=key;
    for(auto& background:sector.get_objects_by_type<Background>())if(!background.get_image().empty()){
      auto sprite=SpriteManager::current()->create(background.get_image());sprite->set_color(background.get_color());
      a.backgrounds.emplace_back(background.get_layer(),std::move(sprite));
    }
  }
  for(auto& layer:a.backgrounds){
    auto& sprite=*layer.second;const float zoom=std::max(320.f/sprite.get_width(),240.f/sprite.get_height());
    const float w=sprite.get_width()*zoom,h=sprite.get_height()*zoom;
    sprite.draw_scaled(c.color(),Rectf((320-w)/2,(240-h)/2,(320+w)/2,(240+h)/2),layer.first);
  }
  c.color().draw_filled_rect(Rectf(0,0,320,240),Color(0,.03f,.08f,.22f),0);

}

}
const FontPtr& menu_font(){return art().menu;}
void prepare(GameSession& g){
  if(g.get_current_level().m_is_in_cutscene)return;
  auto& a=art();
  credited_level_dolls=0;
  // PC commits newly earned dolls on successful level completion. Preview that
  // same total live, subtracting the level's existing credit on replays.
  for(const auto& screen:ScreenManager::current()->get_screen_stack())
    if(auto* map=dynamic_cast<worldmap::WorldMap*>(screen.get()))
      if(auto* tile=map->get_sector().at_object<worldmap::LevelTile>())
        credited_level_dolls=tile->get_statistics().get_tuxdolls();
  TTFSurfaceManager::current()->create_surface(*static_cast<TTFFont*>(a.title_font.get()),g.get_current_level().get_name());
  TTFSurfaceManager::current()->create_surface(*static_cast<TTFFont*>(a.author_font.get()),"by "+g.get_current_level().get_author());
}
void begin_frame(){subtitles.clear();frame_fade=nullptr;frame_effects.clear();boss_heads.clear();}
void queue_fade(ScreenFade& fade){frame_fade=&fade;}
void queue_display_effect(DisplayEffect& effect){frame_effects.push_back(&effect);}
bool drawing_effects(){return rendering_effects;}
bool has_effects(){return frame_fade || !frame_effects.empty();}
void set_shake(const Camera* camera,const Vector& delta){shake_camera=camera;shake_delta=delta;}
Vector bottom_shake(){auto* g=game();if(!g || &g->get_current_sector().get_camera()!=shake_camera)return Vector(0,0);return -shake_delta*g->get_current_sector().get_camera().get_current_scale()*VideoSystem::current()->get_viewport().get_scale();}
void queue_boss(const SurfacePtr& icon,int lives,int maximum){if(icon && lives>0)boss_heads.push_back({icon,lives,maximum});}
void draw_effects(){
  if(!has_effects())return;
  Compositor commands(*VideoSystem::current(),0);auto& c=commands.make_context(true);
  c.transform().viewport=Rect(0,0,320,240);c.transform().scale=1;
  rendering_effects=true;
  for(auto* effect:frame_effects)effect->draw(c);
  if(frame_fade)frame_fade->draw(c);
  rendering_effects=false;
  c.color().render(VideoSystem::current()->get_renderer(),Canvas::ALL);
}
void queue_text(TextObject& text){if(std::find(subtitles.begin(),subtitles.end(),&text)==subtitles.end())subtitles.push_back(&text);}
bool drawing_text(){return rendering_text;}
bool drawing_map_bottom(){return map_bottom;}
void update(){
  static bool was_down=false;
  const bool down=(hidKeysHeld()&KEY_TOUCH)!=0;const bool tapped=down&&!was_down;was_down=down;
  if(tapped && MenuManager::instance().is_active()){
    touchPosition pos{};hidTouchRead(&pos);
    if(ctr_controls::touch(pos.px,pos.py)){dirty=true;return;}
  }
  auto* g=game();if(!tapped || !g || !g->is_active() || !ctr::bottom_hud_enabled() || MenuManager::instance().is_active())return;
  touchPosition pos{};hidTouchRead(&pos);
  const float x=float(pos.px)-reserve_center.x,y=float(pos.py)-reserve_center.y;if(x*x+y*y>reserve_radius*reserve_radius)return;
  auto& sector=g->get_current_sector();BIND_SECTOR(sector);
  const auto players=sector.get_players();if(players.empty() || !players.front()->is_active())return;
  auto* player=players.front();if(player->get_status().get_item_pocket(player)==BONUS_NONE)return;
  player->get_status().give_item_from_pocket(player);dirty=true;
  ctr::log("[HUD] Reserve item released by touch\n");
}
void invalidate(){dirty=true;}
bool needs_redraw(){
  if(had_notice!=ctr_unlock::notice()){dirty=true;had_notice=ctr_unlock::notice();}
  const auto shake=bottom_shake();if(shake!=last_shake){dirty=true;last_shake=shake;}
  bool effects_changed=frame_effects.size()!=last_effect_states.size();
  if(!effects_changed)for(size_t i=0;i<frame_effects.size();++i)
    if(last_effect_states[i].first!=frame_effects[i] || last_effect_states[i].second!=frame_effects[i]->native_fade_alpha()){effects_changed=true;break;}
  if(effects_changed){last_effect_states.clear();for(auto* effect:frame_effects)last_effect_states.emplace_back(effect,effect->native_fade_alpha());dirty=true;}
  bool text_changed=subtitles.size()!=last_text_states.size();
  if(!text_changed)for(size_t i=0;i<subtitles.size();++i)
    if(last_text_states[i].first!=subtitles[i] || last_text_states[i].second!=subtitles[i]->native_draw_state()){text_changed=true;break;}
  if(text_changed){last_text_states.clear();for(auto* text:subtitles)last_text_states.emplace_back(text,text->native_draw_state());dirty=true;}
  if(had_effects!=has_effects()){dirty=true;had_effects=has_effects();}
  if(frame_fade || ctr_unlock::notice())dirty=true;
  auto* g=game();const bool overlay=menu_active() || !subtitles.empty() || !g || g->get_current_level().m_is_in_cutscene;
  if(overlay!=last_overlay){dirty=true;last_overlay=overlay;}
  const auto& screens=ScreenManager::current()->get_screen_stack();
  const void* screen=screens.empty()?nullptr:screens.back().get();
  if(screen!=last_bottom_screen){last_bottom_screen=screen;dirty=true;}
  if(g){
    const bool skip=g->get_current_level().m_skip_cutscene;
    if(skip!=last_skip){last_skip=skip;dirty=true;}
    if(last_file!=g->get_level_file() || last_sector!=&g->get_current_sector()){
      last_file=g->get_level_file();last_sector=&g->get_current_sector();dirty=true;
    }
  }
  auto* menu=MenuManager::instance().current_menu();const auto help=menu?menu->native_help_state():0;
  if(menu!=last_menu || help!=last_help){last_menu=menu;last_help=help;dirty=true;}
  if(overlay){if(map_screen())dirty=true;if(dirty)repaint_left=2;return dirty||repaint_left>0;}
  const auto& status=g->get_savegame().get_player_status();const auto& stats=g->get_current_level().m_stats;
  const int pocket=status.m_item_pockets.empty()?0:status.m_item_pockets.front();
  std::array<int,7> values{status.coins,status.tuxdolls,stats.get_coins(),stats.get_secrets(),pocket,stats.m_total_coins+stats.m_total_secrets,stats.get_tuxdolls()};
  if(pocket!=previous_pocket){
    if(pocket!=BONUS_NONE)bounce_start=std::chrono::steady_clock::now();
    previous_pocket=pocket;
  }
  if(bounce_time()<.55f)dirty=true;
  if(values!=last_values || last_file!=g->get_level_file() || last_sector!=&g->get_current_sector()){dirty=true;last_values=values;last_file=g->get_level_file();last_sector=&g->get_current_sector();}
  if(dirty)repaint_left=2;return dirty||repaint_left>0;
}
void draw_top(DrawingContext& c,GameSession& g){
  if(game()!=&g || g.get_current_level().m_is_in_cutscene || g.get_level_file().find("misc/credits.stl")!=std::string::npos)return;
  auto& a=art();const auto& status=g.get_savegame().get_player_status();
  c.push_transform();c.set_translation(Vector(0,0));c.transform().scale=1.f/VideoSystem::current()->get_viewport().get_scale().x;
  const std::string coins=std::to_string(status.coins);const float cw=Resources::fixed_font->get_text_width(coins);
  image(c,a.coin,c.get_width()-8-cw-40,3,40,34,LAYER_HUD);
  c.color().draw_text(Resources::fixed_font,coins,Vector(c.get_width()-8-cw,11),ALIGN_LEFT,LAYER_HUD,Color(1,1,.4f));
  const auto& timers=g.get_current_sector().get_objects_by_type<LevelTime>();
  if(timers.begin()!=timers.end())
  {
    const float seconds=timers.begin()->get_time();
    const std::string time=std::to_string(std::max(0,int(seconds)));
    const float width=40+Resources::fixed_font->get_text_width(time);const float x=(c.get_width()-width)*.5f;
    image(c,a.time,x,3,40,34,LAYER_HUD);
    c.color().draw_text(Resources::fixed_font,time,Vector(x+40,11),ALIGN_LEFT,LAYER_HUD,Color(1,1,.4f));
  }
  int slots=0;for(const auto& boss:boss_heads)slots+=std::max(boss.lives,boss.max_lives);
  if(slots){const float size=std::min(26.f,360.f/slots);float x=(c.get_width()-size*slots)/2;
    for(const auto& boss:boss_heads){for(int i=0;i<boss.lives;++i)image(c,boss.icon,x+i*size,c.get_height()-size-6,size,size,LAYER_HUD+5);x+=size*std::max(boss.lives,boss.max_lives);}}
  c.pop_transform();
}
void draw_bottom(){
  auto* g=game();
  const auto& stack=ScreenManager::current()->get_screen_stack();
  auto* title_screen=stack.empty()?nullptr:dynamic_cast<TitleScreen*>(stack.back().get());
  const bool credits=g && g->get_level_file().find("misc/credits.stl")!=std::string::npos;
  if(title_screen || credits){
    auto& scene=title_screen?title_screen->native_session():*g;
    Compositor commands(*VideoSystem::current(),0);auto& c=commands.make_context(true);
    c.transform().viewport=Rect(0,0,320,240);c.transform().scale=1;
    auto& a=art();draw_preview(c,scene,a);
    if(title_screen)image(c,a.frame,0,0,320,240,100);
    c.color().render(VideoSystem::current()->get_renderer(),Canvas::ALL);
    dirty=false;if(repaint_left)--repaint_left;return;
  }
  if(auto* map=map_screen()){
    Compositor commands(*VideoSystem::current(),0);auto& c=commands.make_context();
    c.transform().viewport=Rect(0,0,320,240);c.transform().scale=1;
    map_bottom=true;map->get_sector().draw_view(c,Vector(40,240),true);map_bottom=false;
    c.color().render(VideoSystem::current()->get_renderer(),Canvas::ALL);
    dirty=false;if(repaint_left)--repaint_left;return;
  }
  if(!g || g->get_current_level().m_is_in_cutscene || menu_active()){
    Compositor commands(*VideoSystem::current(),0);auto& c=commands.make_context(true);
    c.transform().viewport=Rect(0,0,320,240);c.transform().scale=1;
    c.color().draw_filled_rect(Rectf(0,0,320,240),Color::BLACK,-10000);
    if(g && g->get_current_level().m_is_in_cutscene && !g->get_current_level().m_skip_cutscene)
      c.color().draw_text(Resources::normal_font,"B / START: skip",Vector(160,212),ALIGN_CENTER,LAYER_GUI+100,Color(1,1,.5f));
    rendering_text=true;for(auto* text:subtitles)text->draw(c);rendering_text=false;
    c.color().render(VideoSystem::current()->get_renderer(),Canvas::ALL);
    dirty=false;if(repaint_left)--repaint_left;return;
  }
  auto& a=art();auto& sector=g->get_current_sector();auto& level=g->get_current_level();
  BIND_SECTOR(sector);
  const auto& status=g->get_savegame().get_player_status();
  Compositor commands(*VideoSystem::current(),0);auto& c=commands.make_context(true);
  c.transform().viewport=Rect(0,0,320,240);c.transform().scale=1;
  draw_preview(c,*g,a);
  auto lines=[](const FontPtr& font,const std::string& text){
    std::vector<std::string> result;std::string remaining=text;
    while(!remaining.empty() && result.size()<2){std::string overflow;result.push_back(font->wrap_to_width(remaining,292,&overflow));if(overflow==remaining)break;remaining=std::move(overflow);}return result;
  };
  const auto title=lines(a.title_font,level.get_name());const auto author=lines(a.author_font,"by "+level.get_author());
  float y=8;
  auto shadow_text=[&](const FontPtr& font,const std::string& text,float y,const Color& color){
    c.color().draw_text(font,text,Vector(161,y+1),ALIGN_CENTER,19,Color(0,0,0,.95f));
    c.color().draw_text(font,text,Vector(160,y),ALIGN_CENTER,20,color);
  };
  for(const auto& line:title){shadow_text(a.title_font,line,y,Color(1,1,.45f));y+=16;}
  for(const auto& line:author){shadow_text(a.author_font,line,y,Color(.9f,.9f,.9f));y+=11;}
  const float ring_top=std::max(74.f,y+10);
  const float ring_size=std::clamp(174-ring_top,32.f,100.f);
  reserve_radius=ring_size/2;reserve_center=Vector(160,ring_top+reserve_radius);
  image(c,a.ring,160-reserve_radius,ring_top,ring_size,ring_size);
  const auto pocket=status.m_item_pockets.empty()?BONUS_NONE:status.m_item_pockets.front();
  if(auto i=a.powerups.find(pocket);i!=a.powerups.end()){
    const float t=bounce_time();
    const float size=ring_size*.44f*(t<.55f?1.f+.5f*std::exp(-7*t)*std::sin(18*t):1.f);
    i->second->draw_scaled(c.color(),Rectf(reserve_center-Vector(size/2,size/2),Sizef(size,size)),12);
  }
  image(c,a.doll,12,199,32,32);label(c,std::to_string(status.tuxdolls+std::max(0,level.m_stats.get_tuxdolls()-credited_level_dolls)),49,211,Color(1,1,.5f));
  c.color().draw_filled_rect(Rectf(202,177,313,234),Color(0,0,0,.75f),8,8);
  const auto& stats=level.m_stats;
  label(c,std::to_string(stats.get_coins())+"/"+std::to_string(stats.m_total_coins),210,188,Color(1,1,.5f));
  image(c,a.plain_coin,283,182,24,24);
  label(c,std::to_string(stats.get_secrets())+"/"+std::to_string(stats.m_total_secrets),210,214,Color(1,1,.5f));
  image(c,a.secret,285,207,20,20);
  rendering_text=true;for(auto* text:subtitles)text->draw(c);rendering_text=false;
  c.color().render(VideoSystem::current()->get_renderer(),Canvas::ALL);
  dirty=false;if(repaint_left)--repaint_left;
}
void shutdown(){last_effect_states.clear();last_text_states.clear();boss_heads.clear();frame_effects.clear();frame_fade=nullptr;assets.reset();last_file.clear();last_sector=nullptr;dirty=true;}
}


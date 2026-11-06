// Citro2D drawing backend for the top scene and bottom interface.
#define Profile CtrFriendProfile
#include <citro2d.h>
#undef Profile
#include "gpu_renderer.hpp"
#include "native_hud.hpp"
#include "debug_unlock.hpp"
#include "native_controls.hpp"
#include "loading_label.hpp"
#include "loading.hpp"
#include "gui/menu_manager.hpp"
#include "gui/menu.hpp"
#include "video/sdl/sdl_video_system.hpp"
#include "video/compositor.hpp"
#include "video/drawing_context.hpp"
#include "video/canvas.hpp"
#include "platform.hpp"
#include "performance.hpp"
#include "packed_assets.hpp"
#include "texture_prefetch.hpp"
#include "video/viewport.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <map>

namespace ctr_gpu {
namespace {
C3D_RenderTarget* target = nullptr;
C3D_RenderTarget* bottom_target=nullptr;
C3D_Tex loading_texture{};
bool loading_texture_ready=false;
Tex3DS_SubTexture loading_subtexture{},loading_title_subtexture{};
int screen_width=400;
bool frame_open = false;
Vector scale(1, 1), offset(0, 0);
Rect current_clip(0, 0, 400, 240);
Blend current_blend = Blend::BLEND;
bool have_blend = false;
std::vector<C3D_Tex> retired_textures;
unsigned frames = 0;
u64 image_quads=0, texture_switches=0;
const C3D_Tex* previous_image=nullptr;
void count_image(const C3D_Tex* texture){
  if(!ctr_perf::enabled())return;
  ++image_quads;if(texture!=previous_image){++texture_switches;previous_image=texture;}
}
u64 interval_start = 0;
PackedAssets texture_pack("3ds-gpu");
u32 rgba(const Color& c, float alpha = 1) {
  return C2D_Color32f(c.red, c.green, c.blue, c.alpha * alpha);
}
Vector physical(const Vector& p) { return p * scale + offset; }
unsigned pow2(unsigned n) { unsigned out = 8; while (out < n) out *= 2; return out; }
unsigned morton(unsigned x, unsigned y) {
  return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) |
         ((x & 4) << 2) | ((y & 4) << 3);
}
struct Tile {
  C3D_Tex gpu{};
  C3D_Tex* shared_gpu=nullptr;
  std::shared_ptr<Texture> atlas_owner;
  float u=0, v=0;
  C3D_Tex* handle() {return shared_gpu?shared_gpu:&gpu;}
  int x, y, w, h, sample;
  float inverse_width=0, inverse_height=0;
  ~Tile() {
    if (gpu.data) {
      if (frame_open) C2D_Flush();
      retired_textures.push_back(gpu);
    }
  }
};
}
struct Texture { std::vector<std::unique_ptr<Tile>> tiles; mutable bool smooth=false; bool prepared_edges=false; };

void init() {
  gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
  if (!C3D_Init(2 * C3D_DEFAULT_CMDBUF_SIZE)) throw std::runtime_error("Citro3D init failed");
  if (!C2D_Init(8192)) { C3D_Fini(); throw std::runtime_error("Citro2D init failed"); }
  C2D_Prepare();
  C2D_SetTintMode(C2D_TintMult);
  target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  if (!target) { C2D_Fini(); C3D_Fini(); throw std::runtime_error("GPU top target failed"); }
  ctr::log("[GPU] Citro2D hardware rendering ready\n");
  show_loading();
}
void show_loading(){
  if(!target)return;
  ctr::log("[Loading] Begin static screen\n");ctr::flush_log();
  if(frame_open){C2D_Flush();C3D_FrameEnd(0);frame_open=false;}
  ctr::set_bottom_hud(true);
  if(!loading_texture_ready){
    if(!C3D_TexInit(&loading_texture,loading_label_tw,loading_label_th,GPU_RGBA8))throw std::runtime_error("Loading label allocation failed");
    std::memcpy(loading_texture.data,loading_label_pixels,sizeof(loading_label_pixels));
    C3D_TexSetFilter(&loading_texture,GPU_NEAREST,GPU_NEAREST);C3D_TexFlush(&loading_texture);
    loading_subtexture.width=loading_label_w;loading_subtexture.height=loading_label_h;
    loading_subtexture.left=0;loading_subtexture.right=float(loading_label_w)/loading_label_tw;
    loading_subtexture.top=1;loading_subtexture.bottom=1.f-float(loading_label_h)/loading_label_th;
    loading_title_subtexture.width=loading_title_w;loading_title_subtexture.height=loading_title_h;
    loading_title_subtexture.left=float(loading_title_x)/loading_label_tw;
    loading_title_subtexture.right=float(loading_title_x+loading_title_w)/loading_label_tw;
    loading_title_subtexture.top=1;loading_title_subtexture.bottom=1.f-float(loading_title_h)/loading_label_th;
    loading_texture_ready=true;
  }
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  if(!bottom_target)bottom_target=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
  for(int screen=0;screen<2;++screen){
    auto* out=screen?bottom_target:target;if(!out)continue;
    C2D_TargetClear(out,C2D_Color32(0,0,0,255));C2D_SceneBegin(out);C2D_ViewReset();
    C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);have_blend=false;blend(Blend::BLEND);
    C2D_Image label{&loading_texture,screen?&loading_subtexture:&loading_title_subtexture};
    C2D_DrawImageAt(label,(screen?160:200)-(screen?loading_label_w:loading_title_w)/2.f,120-(screen?loading_label_h:loading_title_h)/2.f,.5f);
    C2D_Flush();
  }
  C3D_FrameEnd(0);C3D_FrameSync();
  ctr::log("[Loading] Static screen ready\n");ctr::flush_log();
  have_blend=false;current_clip=Rect(-1,-1,-1,-1);
}

void shutdown() {
  ctr_prefetch::clear();
  if (!target) return;
  if (frame_open) { C2D_Flush(); C3D_FrameEnd(0); frame_open = false; }
  C3D_FrameSync();
  ctr::set_bottom_hud(false); // No GPU work may overwrite the restored console.
  for (auto& texture : retired_textures) C3D_TexDelete(&texture);
  retired_textures.clear();
  if(loading_texture_ready){C3D_TexDelete(&loading_texture);loading_texture_ready=false;}
  C2D_Fini();
  if(bottom_target){C3D_RenderTargetDelete(bottom_target);bottom_target=nullptr;}
  C3D_RenderTargetDelete(target);
  target = nullptr;
  C3D_Fini();
  texture_pack.close();
}
void collect_unused() {
  if (frame_open) {C2D_Flush();C3D_FrameEnd(0);frame_open=false;}
  C3D_FrameSync();
  for (auto& texture : retired_textures) C3D_TexDelete(&texture);
  retired_textures.clear();
}
void begin(const Viewport& viewport) {
  static bool first=true;
  if(first) ctr::log("[GPU] First frame begin\n");
  screen_width=400;previous_image=nullptr;
  scale = viewport.get_scale();
  const auto& rect = viewport.get_rect();
  offset = Vector(rect.left, rect.top);
  { ctr_perf::Scope timer(ctr_perf::Wait); C3D_FrameBegin(C3D_FRAME_SYNCDRAW); }
  if(first){ctr::log("[GPU] Frame synchronization OK\n");ctr::flush_log();first=false;}
  for (auto& texture : retired_textures) C3D_TexDelete(&texture);
  retired_textures.clear();
  frame_open = true;
  C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
  C2D_SceneBegin(target);
  C2D_ViewReset();
  have_blend = false;
  current_clip = Rect(-1, -1, -1, -1);
  clip(nullptr);
}
void wait_idle(){if(target)C3D_FrameSync();}
void present() {
  if (!frame_open) return;
  if(ctr::bottom_hud_enabled() && ctr_hud::needs_redraw()) {
    C2D_Flush();
    if(!bottom_target)bottom_target=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
    if(!bottom_target)throw std::runtime_error("Bottom HUD target failed");
    C2D_TargetClear(bottom_target,C2D_Color32(0,0,0,255));C2D_SceneBegin(bottom_target);C2D_ViewReset();
    screen_width=320;scale=Vector(1,1);offset=ctr_hud::bottom_shake();have_blend=false;
    current_clip=Rect(-1,-1,-1,-1);clip(nullptr);
    ctr_hud::draw_bottom();
    // Only contextual help belongs below; menu controls stay on top.
    auto& video=static_cast<SDLVideoSystem&>(*VideoSystem::current());
    const int saved_mode=video.native_viewport_mode();
    video.set_native_viewport(4);
    auto& menus=MenuManager::instance();
    Compositor ui(video,0);auto& context=ui.make_context(true);
    scale=Vector(.5f,.5f);offset=ctr_hud::bottom_shake();current_clip=Rect(-1,-1,-1,-1);clip(nullptr);
    if(auto* menu=menus.current_menu()){menu->draw_help(context);ctr_controls::draw(context);}
    ctr_unlock::draw_notice(context);
    context.color().render(video.get_renderer(),Canvas::ALL);
    video.set_native_viewport(saved_mode);
    scale=Vector(1,1);offset=Vector(0,0);current_clip=Rect(-1,-1,-1,-1);clip(nullptr);
    ctr_hud::draw_effects();
  }
  C2D_Flush();
  C3D_FrameEnd(0);
  frame_open = false;
  // Work while the submitted GPU frame is in flight, instead of delaying the
  // first visible draw. Preserve 12 MiB of linear memory for required assets.
  if (linearSpaceFree() >= 12*1024*1024) ctr_prefetch::pump();
  ctr_loading::frame_presented();
  ctr::flush_debug_console();
  if (!interval_start) interval_start = osGetTime();
  ++frames;
  const auto now = osGetTime();
  if (now - interval_start >= 5000) {
    if(ctr_perf::enabled())ctr::log("[GPU] %.1f FPS\nLinear: %lu KiB\n", 1000.0 * frames / (now - interval_start),
             static_cast<unsigned long>(linearSpaceFree() / 1024));
    if(ctr_perf::enabled())ctr::log("[GPU] %.1f image quads, %.1f texture switches per frame\n",double(image_quads)/frames,double(texture_switches)/frames);
    image_quads=texture_switches=0;frames = 0; interval_start = now;
  }
}
void clip(const Rect* logical) {
  Rect r(0, 0, screen_width, 240);
  if (logical) {
    const auto a = physical(Vector(logical->left, logical->top));
    const auto b = physical(Vector(logical->right, logical->bottom));
    r = Rect(std::clamp(int(std::floor(a.x)), 0, screen_width), std::clamp(int(std::floor(a.y)), 0, 240),
             std::clamp(int(std::ceil(b.x)), 0, screen_width), std::clamp(int(std::ceil(b.y)), 0, 240));
  }
  if (r.left == current_clip.left && r.top == current_clip.top &&
      r.right == current_clip.right && r.bottom == current_clip.bottom) return;
  C2D_Flush();
  // Tilted top framebuffer: X is screen Y, Y is inverted screen X.
  C3D_SetScissor(GPU_SCISSOR_NORMAL, r.top, screen_width - r.right, r.bottom, screen_width - r.left);
  current_clip = r;
}
void blend(const Blend& mode) {
  if (have_blend && mode == current_blend) return;
  C2D_Flush();
  if (mode == Blend::NONE)
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
  else if (mode == Blend::ADD)
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ONE, GPU_ONE);
  else if (mode == Blend::MOD)
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_ONE, GPU_ZERO);
  else
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
  have_blend = true; current_blend = mode;
}
bool allow_optional_warmup() { return linearSpaceFree() >= 14*1024*1024; }
bool visible(const Rectf& destination, float angle) {
  if (destination.empty() || current_clip.empty()) return false;
  if (angle == 0) {
    const auto a = physical(destination.p1()), b = physical(destination.p2());
    return b.x > current_clip.left && a.x < current_clip.right &&
           b.y > current_clip.top && a.y < current_clip.bottom;
  }
  const auto center = physical((destination.p1() + destination.p2()) * .5f);
  const float radians = angle * 3.14159265358979323846f / 180.f;
  const float c = std::abs(std::cos(radians)), s = std::abs(std::sin(radians));
  const float w = destination.get_width() * std::abs(scale.x) * .5f;
  const float h = destination.get_height() * std::abs(scale.y) * .5f;
  const float rx = c*w+s*h, ry = s*w+c*h;
  return center.x+rx > current_clip.left && center.x-rx < current_clip.right &&
         center.y+ry > current_clip.top && center.y-ry < current_clip.bottom;
}
std::shared_ptr<Texture> load_cached(const std::string& filename, const std::optional<Rect>& rect) {
  std::string key = filename;
  if (rect) key += '|' + std::to_string(rect->left) + ',' + std::to_string(rect->top) + ',' +
                  std::to_string(rect->get_width()) + ',' + std::to_string(rect->get_height());
  static std::map<std::string,std::weak_ptr<Texture>> atlas_cache;
  const bool atlas_page=key.rfind("/3ds-atlas/",0)==0;
  if(atlas_page){auto found=atlas_cache.find(key);if(found!=atlas_cache.end())if(auto page=found->second.lock())return page;}
  std::vector<unsigned char> bytes;
  if (!texture_pack.read(key, bytes)) return {};
  try {
    size_t cursor = 0;
    auto take = [&](void* out, size_t size) {
      if (cursor > bytes.size() || size > bytes.size()-cursor) throw std::runtime_error("truncated GPU cache");
      std::memcpy(out, bytes.data()+cursor, size); cursor += size;
    };
    char magic[4]; uint32_t count;
    take(magic, 4);
    if(std::memcmp(magic,"TX03",4)==0){
      uint16_t fields[6];take(fields,sizeof(fields));
      if(!fields[0] || !fields[1] || !fields[2] || fields[2]>128 || (fields[2]&(fields[2]-1)) || !fields[5] || fields[5]>256)
        throw std::runtime_error("Invalid atlas reference");
      std::string page_key(fields[5],'\0');take(page_key.data(),page_key.size());
      if(page_key.rfind("/3ds-atlas/",0)!=0 || cursor!=bytes.size())throw std::runtime_error("Invalid atlas page key");
      auto owner=load_cached(page_key,std::nullopt);
      if(!owner || owner->tiles.size()!=1)throw std::runtime_error("Missing atlas page");
      auto* page=owner->tiles.front()->handle();
      if(fields[3]+(fields[0]+fields[2]-1)/fields[2]>page->width || fields[4]+(fields[1]+fields[2]-1)/fields[2]>page->height)
        throw std::runtime_error("Atlas region outside page");
      auto tile=std::make_unique<Tile>();tile->x=tile->y=0;
      tile->w=fields[0];tile->h=fields[1];tile->sample=fields[2];
      tile->u=float(fields[3])/page->width;tile->v=float(fields[4])/page->height;
      tile->inverse_width=1.f/(page->width*tile->sample);tile->inverse_height=1.f/(page->height*tile->sample);
      tile->shared_gpu=page;tile->atlas_owner=std::move(owner);
      auto result=std::make_shared<Texture>();result->prepared_edges=true;result->tiles.push_back(std::move(tile));return result;
    }
    take(&count, 4);
    if ((std::memcmp(magic, "TX01", 4) && std::memcmp(magic, "TX02", 4)) || count > 4096) throw std::runtime_error("invalid GPU cache header");
    auto result = std::make_shared<Texture>();
    result->prepared_edges=std::memcmp(magic,"TX02",4)==0;
    for (uint32_t i=0; i<count; ++i) {
      uint16_t fields[7]; take(fields, sizeof(fields));
      const unsigned tw=fields[5], th=fields[6];
      if (tw<8 || th<8 || tw>512 || th>512 || (tw&(tw-1)) || (th&(th-1)) ||
          (!fields[4] || fields[4]>128 || (fields[4]&(fields[4]-1))) || !fields[2] || !fields[3])
        throw std::runtime_error("invalid GPU cache tile");
      auto tile=std::make_unique<Tile>();
      tile->x=fields[0]; tile->y=fields[1]; tile->w=fields[2]; tile->h=fields[3]; tile->sample=fields[4];
      tile->inverse_width=1.f/(tw*tile->sample);tile->inverse_height=1.f/(th*tile->sample);
      if (!C3D_TexInit(&tile->gpu, tw, th, GPU_RGBA8)) throw std::runtime_error("GPU cache allocation failed");
      take(tile->gpu.data, tw*th*4);
      C3D_TexSetFilter(&tile->gpu, GPU_NEAREST, GPU_NEAREST);
      C3D_TexSetWrap(&tile->gpu, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
      C3D_TexFlush(&tile->gpu);
      result->tiles.push_back(std::move(tile));
    }
    if (cursor != bytes.size()) throw std::runtime_error("unexpected GPU cache data");
    if(atlas_page)atlas_cache[key]=result;
    return result;
  } catch (const std::exception& error) {
    ctr::log("[Cache] %s: %s\n", filename.c_str(), error.what());
    return {};
  }
}
std::shared_ptr<Texture> upload(const SDL_Surface& image) {
  auto pixels = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>(
    SDL_ConvertSurfaceFormat(const_cast<SDL_Surface*>(&image), SDL_PIXELFORMAT_RGBA8888, 0), SDL_FreeSurface);
  if (!pixels) throw std::runtime_error(SDL_GetError());
  auto result = std::make_shared<Texture>();
  bool visible = false;
  for (int y=0; y<image.h && !visible; ++y) {
    const auto* row=reinterpret_cast<const u32*>(static_cast<const char*>(pixels->pixels)+y*pixels->pitch);
    for (int x=0; x<image.w; ++x) if (row[x] & 255) { visible=true; break; }
  }
  if (!visible) return result;
  // Large desktop images only need half resolution on the 400x240 panel.
  // Geometry and source rectangles remain in the original coordinate space.
  const int sample = 1; // Packaged art is resized offline; dynamic/fallback images stay native.
  const int step=512*sample;
  for (int y = 0; y < image.h; y += step) for (int x = 0; x < image.w; x += step) {
    auto tile = std::make_unique<Tile>();
    tile->x = x; tile->y = y;
    tile->w = std::min(step, image.w - x); tile->h = std::min(step, image.h - y); tile->sample=sample;
    const int pw=(tile->w+sample-1)/sample, ph=(tile->h+sample-1)/sample;
    const unsigned tw = pow2(pw), th = pow2(ph);
    tile->inverse_width=1.f/(tw*sample);tile->inverse_height=1.f/(th*sample);
    if (!C3D_TexInit(&tile->gpu, tw, th, GPU_RGBA8)) throw std::runtime_error("GPU texture allocation failed");
    std::memset(tile->gpu.data, 0, tw * th * 4);
    C3D_TexSetFilter(&tile->gpu, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&tile->gpu, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    auto* dst = static_cast<u32*>(tile->gpu.data);
    for (int row = 0; row < ph; ++row) {
      const auto* src = reinterpret_cast<const u32*>(static_cast<const char*>(pixels->pixels) + (y + row*sample) * pixels->pitch);
      for (int col = 0; col < pw; ++col) {
        const unsigned pos = ((row / 8) * (tw / 8) + col / 8) * 64 + morton(col, row);
        dst[pos] = src[x + col*sample];
      }
    }
    C3D_TexFlush(&tile->gpu);
    result->tiles.push_back(std::move(tile));
  }
  return result;
}
void smooth_background(const Texture& texture) {
  if (texture.smooth) return;
  if (frame_open) C2D_Flush();
  for (const auto& tile : texture.tiles) {
    if(tile->atlas_owner)continue; // Atlases keep nearest filtering; each region has a gutter.
    const unsigned tw=tile->gpu.width, th=tile->gpu.height;
    const unsigned pw=(tile->w+tile->sample-1)/tile->sample;
    const unsigned ph=(tile->h+tile->sample-1)/tile->sample;
    auto* pixels=static_cast<u32*>(tile->gpu.data);
    auto position=[tw](unsigned x,unsigned y) {return ((y/8)*(tw/8)+x/8)*64+morton(x,y);};
    // Clamp the unused POT padding to the artwork edge. Linear filtering must
    // not blend clouds/sky against transparent padding along a partial tile.
    if(!texture.prepared_edges)for (unsigned y=0;y<th;++y) for (unsigned x=0;x<tw;++x)
      if (x>=pw || y>=ph) pixels[position(x,y)]=pixels[position(std::min(x,pw-1),std::min(y,ph-1))];
    C3D_TexSetFilter(&tile->gpu,GPU_LINEAR,GPU_LINEAR);
    C3D_TexFlush(&tile->gpu);
  }
  texture.smooth=true;
}
void image(const Texture& texture, const Rectf& src, const Rectf& dst,
           float angle, Flip flip, const Color& color, float alpha) {
  if (src.empty() || dst.empty()) return;
  C2D_ImageTint tint;
  C2D_PlainImageTint(&tint, rgba(color, alpha), 1.0f);
  const bool hf = flip & HORIZONTAL_FLIP, vf = flip & VERTICAL_FLIP;
  // Most sprites, glyphs and terrain have a single tile. Their source region
  // needs no clipping/splitting or per-rectangle floating-point divisions.
  if (texture.tiles.size()==1) {
    const auto& t=texture.tiles.front();
    if (src.get_left()>=t->x && src.get_top()>=t->y &&
        src.get_right()<=t->x+t->w && src.get_bottom()<=t->y+t->h) {
      const auto p=physical(dst.p1());
      if (angle==0 && (p.x+dst.get_width()*scale.x<=current_clip.left || p.x>=current_clip.right ||
          p.y+dst.get_height()*scale.y<=current_clip.top || p.y>=current_clip.bottom)) return;
      Tex3DS_SubTexture sub{};sub.width=1;sub.height=1;
      sub.left=t->u+(src.get_left()-t->x)*t->inverse_width;
      sub.right=t->u+(src.get_right()-t->x)*t->inverse_width;
      sub.top=1.f-t->v-(src.get_top()-t->y)*t->inverse_height;
      sub.bottom=1.f-t->v-(src.get_bottom()-t->y)*t->inverse_height;
      if (hf) std::swap(sub.left,sub.right);
      if (vf) std::swap(sub.top,sub.bottom);
      count_image(t->handle());
      C2D_Image image{t->handle(),&sub};
      if(angle==0)C2D_DrawImageAt(image,p.x,p.y,.5f,&tint,dst.get_width()*scale.x,dst.get_height()*scale.y);
      else {
        const auto center=physical((dst.p1()+dst.p2())*.5f);
        // Vertex rotation keeps consecutive particles in the same GPU batch;
        // view-matrix rotation forced two flushes per flake/sprite.
        C2D_DrawImageAtRotated(image,center.x,center.y,.5f,angle*(3.14159265358979323846f/180.f),&tint,dst.get_width()*scale.x,dst.get_height()*scale.y);
      }
      return;
    }
  }
  const auto pivot = physical((dst.p1() + dst.p2()) * .5f);
  if (angle != 0) {
    C2D_Flush(); // Isolate sprite transforms from subsequent help/HUD icons.
    C2D_ViewTranslate(pivot.x, pivot.y);
    C2D_ViewRotateDegrees(angle);
    C2D_ViewTranslate(-pivot.x, -pivot.y);
  }
  for (const auto& t : texture.tiles) {
    const float l = std::max(src.get_left(), float(t->x)), top = std::max(src.get_top(), float(t->y));
    const float r = std::min(src.get_right(), float(t->x + t->w)), b = std::min(src.get_bottom(), float(t->y + t->h));
    if (l >= r || top >= b) continue;
    const float dx = hf ? (src.get_right() - r) / src.get_width() : (l - src.get_left()) / src.get_width();
    const float dy = vf ? (src.get_bottom() - b) / src.get_height() : (top - src.get_top()) / src.get_height();
    const auto p = physical(Vector(dst.get_left() + dx * dst.get_width(), dst.get_top() + dy * dst.get_height()));
    const float w = (r-l) * dst.get_width() / src.get_width() * scale.x;
    const float h = (b-top) * dst.get_height() / src.get_height() * scale.y;
    if (angle == 0 && (p.x + w <= current_clip.left || p.x >= current_clip.right ||
                      p.y + h <= current_clip.top || p.y >= current_clip.bottom)) continue;
    Tex3DS_SubTexture sub{};
    sub.width = std::max<u16>(1, static_cast<u16>(r-l)); sub.height = std::max<u16>(1, static_cast<u16>(b-top));
    sub.left = t->u+(l-t->x)*t->inverse_width; sub.right = t->u+(r-t->x)*t->inverse_width;
    sub.top = 1.0f-t->v-(top-t->y)*t->inverse_height; sub.bottom = 1.0f-t->v-(b-t->y)*t->inverse_height;
    if (hf) std::swap(sub.left, sub.right);
    if (vf) std::swap(sub.top, sub.bottom);
    count_image(t->handle());
    C2D_Image image{t->handle(), &sub};
    C2D_DrawImageAt(image, p.x, p.y, .5f, &tint, w/sub.width, h/sub.height);
  }
  if (angle != 0) {C2D_Flush();C2D_ViewReset();}
}
void rectangle(const Rectf& r, const Color& tl, const Color& tr, const Color& bl, const Color& br, float alpha) {
  const auto p = physical(r.p1());
  if (r.get_width() <= 0 || r.get_height() <= 0) return;
  C2D_DrawRectangle(p.x, p.y, .5f, r.get_width()*scale.x, r.get_height()*scale.y,
                    rgba(tl,alpha), rgba(tr,alpha), rgba(bl,alpha), rgba(br,alpha));
}
void line(const Vector& a, const Vector& b, const Color& color, float alpha) {
  const auto p = physical(a), q = physical(b); const auto c = rgba(color, alpha);
  C2D_DrawLine(p.x,p.y,c,q.x,q.y,c,1,.5f);
}
void triangle(const Vector& a, const Vector& b, const Vector& c, const Color& color, float alpha) {
  const auto p=physical(a), q=physical(b), r=physical(c); const auto col=rgba(color,alpha);
  C2D_DrawTriangle(p.x,p.y,col,q.x,q.y,col,r.x,r.y,col,.5f);
}
}

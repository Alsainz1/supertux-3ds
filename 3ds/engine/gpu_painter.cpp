// 3DS implementation of the engine Painter, using Citro2D instead of SDL blits.
#include "video/sdl/sdl_painter.hpp"
#include "video/sdl/sdl_texture.hpp"
#include "video/sdl/sdl_video_system.hpp"
#include "video/drawing_request.hpp"
#include "gpu_renderer.hpp"
#include <algorithm>
#include <cmath>

SDLPainter::SDLPainter(SDLVideoSystem& system, Renderer& renderer, SDL_Renderer* sdl) :
  m_video_system(system), m_renderer(renderer), m_sdl_renderer(sdl), m_cliprect() {}

void SDLPainter::draw_texture(const DrawingRequest& req) {
  const auto& r = std::get<TextureRequest>(req.request);
  const auto& texture = static_cast<const SDLTexture&>(*r.texture);
  if (req.alpha <= 0 || r.color.alpha <= 0) return;
  const ctr_gpu::Texture* gpu = nullptr;
  const auto animation = texture.get_sampler().get_animate();
  for (size_t i = 0; i < r.srcrects.size(); ++i) {
    const auto& src = r.srcrects[i];
    const auto& dst = r.dstrects[i];
    // Cull before decoding/uploading: offscreen animation frames used to
    // trigger SD reads and GPU allocations even though no pixels were drawn.
    if (!ctr_gpu::visible(dst, r.angles[i])) continue;
    if (!gpu) { gpu = &texture.get_gpu_texture(); ctr_gpu::blend(req.blend); }
    if (animation.x == 0 && animation.y == 0) {
      ctr_gpu::image(*gpu, src, dst, r.angles[i], req.flip, r.color, req.alpha);
    } else {
      // Split scrolling texture coordinates at wrap boundaries.
      extern float g_game_time;
      const float tw=texture.get_image_width(), th=texture.get_image_height();
      float sy=std::fmod(src.get_top() + animation.y*g_game_time,th); if(sy<0)sy+=th;
      for(float y=0; y<src.get_height();) {
        const float h=std::min(th-sy,src.get_height()-y);
        float sx=std::fmod(src.get_left() + animation.x*g_game_time,tw); if(sx<0)sx+=tw;
        for(float x=0; x<src.get_width();) {
          const float w=std::min(tw-sx,src.get_width()-x);
          Rectf piece(dst.get_left()+x/src.get_width()*dst.get_width(), dst.get_top()+y/src.get_height()*dst.get_height(),
                      dst.get_left()+(x+w)/src.get_width()*dst.get_width(), dst.get_top()+(y+h)/src.get_height()*dst.get_height());
          ctr_gpu::image(*gpu,Rectf(sx,sy,sx+w,sy+h),piece,r.angles[i],req.flip,r.color,req.alpha);
          x+=w; sx=0;
        }
        y+=h; sy=0;
      }
    }
  }
}
void SDLPainter::draw_gradient(const DrawingRequest& req) {
  const auto& r=std::get<GradientRequest>(req.request);
  ctr_gpu::blend(req.blend);
  if(r.direction==HORIZONTAL || r.direction==HORIZONTAL_SECTOR)
    ctr_gpu::rectangle(r.region,r.top,r.bottom,r.top,r.bottom,req.alpha);
  else ctr_gpu::rectangle(r.region,r.top,r.top,r.bottom,r.bottom,req.alpha);
}
void SDLPainter::draw_filled_rect(const DrawingRequest& req) {
  const auto& r=std::get<FillRectRequest>(req.request);
  ctr_gpu::blend(req.blend);
  const float radius=std::min({r.radius,r.rect.get_width()*.5f,r.rect.get_height()*.5f});
  if(radius<=0) { ctr_gpu::rectangle(r.rect,r.color,r.color,r.color,r.color,req.alpha); return; }
  const float h=r.rect.get_height();
  auto strip=[&](float top,float bottom,float inset){
    if(bottom>top)ctr_gpu::rectangle(Rectf(r.rect.get_left()+inset,r.rect.get_top()+top,r.rect.get_right()-inset,r.rect.get_top()+bottom),r.color,r.color,r.color,r.color,req.alpha);
  };
  // Only the curved edges need scan strips; the straight middle is one quad.
  strip(radius,h-radius,0);
  for(float y=0;y<radius;y+=2){
    const float end=std::min(y+2,radius);
    const float dy=radius-(y+end)*.5f;
    const float inset=radius-std::sqrt(std::max(0.f,radius*radius-dy*dy));
    strip(y,end,inset);strip(h-end,h-y,inset);
  }
}
void SDLPainter::draw_inverse_ellipse(const DrawingRequest& req) {
  const auto& r=std::get<InverseEllipseRequest>(req.request);
  ctr_gpu::blend(req.blend);
  const float width=req.viewport.get_width();
  const float height=req.viewport.get_height();
  const float top=r.pos.y-r.size.y*.5f;
  for(float y=0;y<height;y+=2) {
    const float p=r.size.y>0 ? (y+1-top)/r.size.y*2-1 : 2;
    const float hole=std::abs(p)<1 ? std::sqrt(1-p*p)*r.size.x*.5f : 0;
    const float l=std::clamp(r.pos.x-hole,0.f,width), right=std::clamp(r.pos.x+hole,0.f,width);
    ctr_gpu::rectangle(Rectf(0,y,l,std::min(y+2,height)),r.color,r.color,r.color,r.color,req.alpha);
    ctr_gpu::rectangle(Rectf(right,y,width,std::min(y+2,height)),r.color,r.color,r.color,r.color,req.alpha);
  }
}
void SDLPainter::draw_line(const DrawingRequest& req) {
  const auto& r=std::get<LineRequest>(req.request); ctr_gpu::blend(req.blend);
  ctr_gpu::line(r.pos,r.dest_pos,r.color,req.alpha);
}
void SDLPainter::draw_triangle(const DrawingRequest& req) {
  const auto& r=std::get<TriangleRequest>(req.request); ctr_gpu::blend(req.blend);
  ctr_gpu::triangle(r.pos1,r.pos2,r.pos3,r.color,req.alpha);
}
void SDLPainter::clear(const Color& color) {
  ctr_gpu::blend(Blend::NONE);
  const auto size=m_video_system.get_viewport().get_screen_size();
  const Rectf rect=m_cliprect ? Rectf(float(m_cliprect->x),float(m_cliprect->y),
    float(m_cliprect->x+m_cliprect->w),float(m_cliprect->y+m_cliprect->h)) : Rectf(0,0,size.width,size.height);
  ctr_gpu::rectangle(rect,color,color,color,color);
}
void SDLPainter::set_clip_rect(const Rect& rect) {
  m_cliprect=rect.to_sdl(); ctr_gpu::clip(&rect);
}
void SDLPainter::clear_clip_rect() { m_cliprect.reset(); ctr_gpu::clip(nullptr); }
void SDLPainter::get_pixel(const DrawingRequest& req) const {
  // Lighting is disabled on this backend; the unlit scene is fully illuminated.
  *std::get<GetPixelRequest>(req.request).color_ptr=Color::WHITE;
}

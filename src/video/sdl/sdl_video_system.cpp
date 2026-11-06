//  SuperTux
//  Copyright (C) 2014 Ingo Ruhnke <grumbel@gmail.com>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "video/sdl/sdl_video_system.hpp"
#ifdef __3DS__
#include "native_hud.hpp"
#include "platform.hpp"
#endif

#include <sstream>
#ifdef __3DS__
#include <3ds.h>
#include "gpu_renderer.hpp"
#endif

#include "math/rect.hpp"
#include "supertux/gameconfig.hpp"
#include "supertux/globals.hpp"
#include "util/log.hpp"
#include "video/renderer.hpp"
#include "video/sdl/sdl_screen_renderer.hpp"
#include "video/sdl/sdl_texture.hpp"
#include "video/sdl/sdl_texture_renderer.hpp"
#include "video/sdl_surface.hpp"
#include "video/texture_manager.hpp"

SDLVideoSystem::SDLVideoSystem() :
  m_sdl_renderer(nullptr, &SDL_DestroyRenderer),
  m_viewport(),
  m_renderer(),
  m_lightmap(),
  m_texture_manager()
{
  create_window();

  m_renderer.reset(new SDLScreenRenderer(*this, m_sdl_renderer.get()));
  m_texture_manager.reset(new TextureManager);

  apply_config();
#ifdef __3DS__
  ctr_gpu::init();
#endif
}

SDLVideoSystem::~SDLVideoSystem()
{
#ifdef __3DS__
  ctr_hud::shutdown();
  m_texture_manager.reset();
  m_lightmap.reset();
  m_renderer.reset();
  ctr_gpu::shutdown();
#endif
}

std::string
SDLVideoSystem::get_name() const
{
#ifdef __3DS__
  return "Citro2D GPU (SDL2 input/audio)";
#endif
  SDL_version version;
  SDL_GetVersion(&version);
  std::ostringstream out;
  out << "SDL "
      << static_cast<int>(version.major)
      << "." << static_cast<int>(version.minor)
      << "." << static_cast<int>(version.patch);
  return out.str();
}

void
SDLVideoSystem::create_window()
{
  log_info << "Creating SDLVideoSystem" << std::endl;

#ifdef __3DS__
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // ARM11 software renderer: nearest filtering.
#else
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
#endif
  SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");

  create_sdl_window(0);

  m_sdl_renderer.reset(SDL_CreateRenderer(m_sdl_window.get(), -1, 0));
  if (!m_sdl_renderer)
  {
    std::stringstream msg;
    msg << "Couldn't create SDL_Renderer: " << SDL_GetError();
    throw std::runtime_error(msg.str());
  }
}

void
SDLVideoSystem::apply_config()
{
#ifdef __3DS__
  m_native_viewport_mode=0;
#endif
  apply_video_mode();

  { // apply_viewport
#ifndef __ANDROID__
    Size target_size = (g_config->use_fullscreen && g_config->fullscreen_size != Size(0, 0)) ?
      g_config->fullscreen_size :
      g_config->window_size;
#else
    Size target_size = m_desktop_size;
#endif
    m_viewport = Viewport::from_size(target_size, m_desktop_size);
  }

  m_lightmap.reset(new SDLTextureRenderer(*this, m_sdl_renderer.get(), m_viewport.get_screen_size(), 5));
}

Renderer&
SDLVideoSystem::get_renderer() const
{
  return *m_renderer;
}

Renderer&
SDLVideoSystem::get_lightmap() const
{
  return *m_lightmap;
}

TexturePtr
SDLVideoSystem::new_texture(const SDL_Surface& image, const Sampler& sampler)
{
  return TexturePtr(new SDLTexture(image, sampler));
}

void
SDLVideoSystem::set_vsync(int mode)
{
  log_warning << "Setting vsync not supported by SDL renderer" << std::endl;
}

int
SDLVideoSystem::get_vsync() const
{
  return 0;
}

void
SDLVideoSystem::flip()
{
  m_renderer->flip();
  // The native GPU present path also flushes the bottom debug terminal.
}

SDLSurfacePtr
SDLVideoSystem::make_screenshot()
{
#ifdef __3DS__
  log_warning << "GPU screenshot readback is not implemented" << std::endl;
  return {};
#endif
  int width;
  int height;
  if (SDL_GetRendererOutputSize(m_renderer->get_sdl_renderer(), &width, &height) != 0)
  {
    log_warning << "SDL_GetRenderOutputSize failed: " << SDL_GetError() << std::endl;
    return {};
  }
  else
  {
    SDLSurfacePtr surface = SDLSurface::create_rgba(width, height);

    SDL_LockSurface(surface.get());
    int ret = SDL_RenderReadPixels(m_renderer->get_sdl_renderer(), nullptr,
                                   SDL_PIXELFORMAT_ABGR8888,
                                   surface->pixels,
                                   surface->pitch);
    SDL_UnlockSurface(surface.get());

    if (ret != 0)
    {
      log_warning << "SDL_RenderReadPixels failed: " << SDL_GetError() << std::endl;
      return {};
    }
    else
    {
      return surface;
    }
  }
}

#ifdef __3DS__
void SDLVideoSystem::set_native_viewport(int mode) {
  if(mode==m_native_viewport_mode)return;
  if(m_native_viewport_mode==0)m_regular_viewport=m_viewport;
  if(mode==2)m_viewport=Viewport(Rect(0,0,400,240),Vector(.44f,.44f));
  else if(mode==5)m_viewport=Viewport(Rect(0,0,400,240),Vector(.38f,.38f));
  else if(mode==1)m_viewport=Viewport(Rect(0,0,400,240),Vector(1,1));
  else if(mode==4)m_viewport=Viewport(Rect(0,0,320,240),Vector(.5f,.5f));
  else if(mode==3)m_viewport=Viewport(Rect(0,0,400,240),Vector(.5f,.5f));
  else m_viewport=m_regular_viewport;
  m_native_viewport_mode=mode;
}
#endif

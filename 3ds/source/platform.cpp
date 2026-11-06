#include "platform.hpp"
#include <3ds.h>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <streambuf>
#include <sys/stat.h>
#include <citro3d.h>
#include <vector>
#include <cstring>

namespace {
PrintConsole bottom_console;
FILE* log_file = nullptr;
bool console_dirty = true;
bool hud_enabled=false;
std::vector<u16> hidden_console;
class DebugBuffer final : public std::streambuf {
  std::streamsize xsputn(const char* data, std::streamsize length) override {
    console_dirty = true;
    fwrite(data, 1, length, stdout);
    if (log_file) fwrite(data, 1, length, log_file);
    return length;
  }
  int_type overflow(int_type ch) override {
    if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
    const char c = traits_type::to_char_type(ch);
    xsputn(&c, 1);
    return ch;
  }
  int sync() override {
    fflush(stdout);
    // Keep the SD log buffered. Flushing it on every std::endl can stall a
    // gameplay frame; shutdown/fclose still commits all buffered messages.
    ctr::flush_debug_console();
    return 0;
  }
} debug_buffer;
std::streambuf* old_out;
std::streambuf* old_err;
std::streambuf* old_log;
}

void ctr::init_debug_console() {
  // Keep the standard console and font; gameplay can temporarily use a HUD.
  gfxSetDoubleBuffering(GFX_BOTTOM, false);
  gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
  consoleInit(GFX_BOTTOM, &bottom_console);
  consoleSelect(&bottom_console);
  consoleDebugInit(debugDevice_CONSOLE);
  GSPGPU_FlushDataCache(bottom_console.frameBuffer,320*240*2);
  gfxScreenSwapBuffers(GFX_BOTTOM,false);gspWaitForVBlank();
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  mkdir("sdmc:/3ds", 0777);
  mkdir("sdmc:/3ds/supertux070", 0777);
  log_file = fopen("sdmc:/3ds/supertux070/debug.log", "w");
  old_out = std::cout.rdbuf(&debug_buffer);
  old_err = std::cerr.rdbuf(&debug_buffer);
  old_log = std::clog.rdbuf(&debug_buffer);
  log("SuperTux 0.7.0 / 3DS bring-up\n");
  log("Debug terminal: BOTTOM SCREEN\n");
  log("Log file: %s\n", log_file ? "sdmc:/3ds/supertux070/debug.log" : "unavailable");
}

void ctr::shutdown_debug_console() {
  std::cout.flush(); std::cerr.flush(); std::clog.flush();
  std::cout.rdbuf(old_out); std::cerr.rdbuf(old_err); std::clog.rdbuf(old_log);
  if (log_file) { fclose(log_file); log_file = nullptr; }
}

void ctr::flush_debug_console() {
  if (hud_enabled || !console_dirty) return;
  u16 width, height;
  auto* framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
  const auto format = gfxGetScreenFormat(GFX_BOTTOM);
  const unsigned bytes_per_pixel = format == GSP_RGBA8_OES ? 4 : format == GSP_BGR8_OES ? 3 : 2;
  GSPGPU_FlushDataCache(framebuffer, static_cast<u32>(width) * height * bytes_per_pixel);
  console_dirty = false;
}

void ctr::flush_log(){if(log_file)fflush(log_file);}

void ctr::log(const char* format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  std::cerr << text << std::flush;
}

bool ctr::bottom_hud_enabled(){return hud_enabled;}
void ctr::set_bottom_hud(bool enabled) {
  if(hud_enabled==enabled)return;
  C3D_FrameSync();
  if(enabled) {
    if(hidden_console.empty())hidden_console.resize(320*240);
    std::memcpy(hidden_console.data(),bottom_console.frameBuffer,320*240*2);
    bottom_console.frameBuffer=hidden_console.data();
    gfxSetScreenFormat(GFX_BOTTOM,GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM,true);
  } else {
    gfxSetDoubleBuffering(GFX_BOTTOM,false);
    gfxSetScreenFormat(GFX_BOTTOM,GSP_RGB565_OES);
    u16 w,h;bottom_console.frameBuffer=reinterpret_cast<u16*>(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h));
    std::memcpy(bottom_console.frameBuffer,hidden_console.data(),320*240*2);
    // Format changes are only presented on a buffer swap. Without this,
    // RGB565 console pixels were scanned out as BGR8 (multicolored garbage).
    GSPGPU_FlushDataCache(bottom_console.frameBuffer,320*240*2);
    gfxScreenSwapBuffers(GFX_BOTTOM,false);gspWaitForVBlank();
    console_dirty=true;
  }
  hud_enabled=enabled;
}

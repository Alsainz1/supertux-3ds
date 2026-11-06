extern "C" {
#include <3ds/gfx.h>
#include <3ds/os.h>
#include <3ds/services/apt.h>
#include <3ds/services/hid.h>
#include <3ds/services/gspgpu.h>
extern u32 __ctru_heap_size;
}
#include <SDL.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include "platform.hpp"
#include "loading.hpp"
#include "supertux/main.hpp"
int main(int, char**) {
  ctr_loading::protect_startup();
  hidInit(); // Keep input valid after SDL teardown, including fatal/startup errors.
  gfxInitDefault();
  ctr::init_debug_console();
  osSetSpeedupEnable(true);
  ctr::log("[3DS] Application heap: %lu MiB\n", static_cast<unsigned long>(__ctru_heap_size / (1024 * 1024)));
  SDL_SetMainReady();
  ctr::log("[3DS] Starting actual SuperTux 0.7.0 engine\n");
  int result = 1;
  try {
    auto engine = std::make_unique<Main>();
    char executable[] = "sdmc:/3ds/supertux070/SuperTux-0.7.0-engine.3dsx";
    char data_flag[] = "--datadir";
    char data_path[] = "sdmc:/3ds/supertux070/data";
    char user_flag[] = "--userdir";
    char user_path[128] = "sdmc:/3ds/supertux070/save";
    char renderer_flag[] = "--renderer";
    char renderer[] = "sdl";
    char* args[] = {executable, data_flag, data_path, user_flag, user_path, renderer_flag, renderer, nullptr};
    result = engine->run(7, args);
    engine.reset();
  } catch (const std::exception& error) {
    ctr::log("[3DS FATAL] %s\n", error.what());
  }
  ctr_loading::shutdown();
  ctr::log("[3DS] Engine returned %d\n", result);
  if (result != 0) {
    ctr::log("START: exit\n");
    ctr::flush_log();
    while (aptMainLoop()) {
      hidScanInput();
      if (hidKeysDown() & KEY_START) break;
      gfxFlushBuffers();
      gspWaitForVBlank();
    }
  }
  ctr::shutdown_debug_console();
  gfxExit();hidExit();
  return result;
}

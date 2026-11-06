#include "platform.hpp"
#include "level_probe.hpp"
#include <3ds.h>
#include <citro2d.h>
#include <squirrel.h>
#include <sqstdaux.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void script_print(HSQUIRRELVM, const SQChar* format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  ctr::log("%s", text);
}

void probe_squirrel() {
  HSQUIRRELVM vm = sq_open(256);
  if (!vm) throw std::runtime_error("Squirrel VM allocation failed");
  sq_setprintfunc(vm, script_print, script_print);
  sqstd_seterrorhandlers(vm);
  // Exercise compiler, closure calls, integer arithmetic and result conversion.
  const char* script = "local sum = 0; for(local i = 0; i < 10; ++i) sum += i; return sum;";
  bool ok = SQ_SUCCEEDED(sq_compilebuffer(vm, script, strlen(script), "3ds-probe", SQTrue));
  SQInteger result = 0;
  if (ok) {
    sq_pushroottable(vm);
    ok = SQ_SUCCEEDED(sq_call(vm, 1, SQTrue, SQTrue)) &&
         SQ_SUCCEEDED(sq_getinteger(vm, -1, &result)) && result == 45;
  }
  sq_close(vm);
  if (!ok) throw std::runtime_error("Squirrel script execution failed");
  ctr::log("[PASS] Squirrel compiled and ran: 45\n");
}
}

int main() {
  gfxInitDefault();
  ctr::init_debug_console();
  bool is_new = false;
  if (R_SUCCEEDED(APT_CheckNew3DS(&is_new)) && is_new) osSetSpeedupEnable(true);
  ctr::log("Hardware: %s\n", is_new ? "New 3DS (speedup enabled)" : "Original 3DS / emulator");

  bool ready = false;
  bool romfs_ready = false;
  bool c3d_ready = false;
  bool c2d_ready = false;
  C3D_RenderTarget* top = nullptr;
  C2D_SpriteSheet logo = nullptr;
  C2D_TextBuf text_buffer = nullptr;
  C2D_Text heading{}, status{}, help{};
  try {
    Result result = romfsInit();
    if (R_FAILED(result)) throw std::runtime_error("RomFS mount failed");
    romfs_ready = true;
    ctr::log("[PASS] Embedded RomFS mounted\n");
    const auto level = probe_level("romfs:/levels/welcome_antarctica.stl");
    ctr::log("[PASS] 0.7.0 level parser\n%s\nSectors: %u  Tilemaps: %u\nTile entries: %u\n",
             level.name.c_str(), level.sectors, level.tilemaps, level.tiles);
    probe_squirrel();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) throw std::runtime_error("Citro3D initialization failed");
    c3d_ready = true;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) throw std::runtime_error("Citro2D initialization failed");
    c2d_ready = true;
    C2D_Prepare();
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    if (!top) throw std::runtime_error("Top screen render target failed");
    logo = C2D_SpriteSheetLoad("romfs:/logo.t3x");
    if (!logo) throw std::runtime_error("0.7.0 logo texture load failed");
    text_buffer = C2D_TextBufNew(512);
    if (!text_buffer) throw std::runtime_error("Text buffer allocation failed");
    C2D_TextParse(&heading, text_buffer, "0.7.0 - Nintendo 3DS");
    C2D_TextParse(&status, text_buffer, "Initial port test - gameplay pending");
    C2D_TextParse(&help, text_buffer, "A: repeat checks     START: exit");
    C2D_TextOptimize(&heading); C2D_TextOptimize(&status); C2D_TextOptimize(&help);
    ctr::log("[PASS] Top screen GPU initialized\n");
    ctr::log("Initial tests passed.\nGameplay engine not integrated yet.\nA: rerun checks / START: exit\n");
    ready = true;
  } catch (const std::exception& error) {
    ctr::log("[FAIL] %s\nSTART: exit\n", error.what());
  }

  while (aptMainLoop()) {
    hidScanInput();
    const u32 pressed = hidKeysDown();
    if (pressed & KEY_START) break;
    if (ready && (pressed & KEY_A)) {
      try {
        const auto level = probe_level("romfs:/levels/welcome_antarctica.stl");
        ctr::log("[PASS] Level reloaded: %u tiles\n", level.tiles);
        probe_squirrel();
      } catch (const std::exception& error) { ctr::log("[FAIL] %s\n", error.what()); }
    }
    if (ready) {
      C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
      C2D_TargetClear(top, C2D_Color32(16, 30, 54, 255));
      C2D_SceneBegin(top);
      const auto image = C2D_SpriteSheetGetImage(logo, 0);
      C2D_DrawImageAt(image, (400.0f - image.subtex->width) / 2.0f, 22, 0);
      const u32 white = C2D_Color32(255, 255, 255, 255);
      C2D_DrawText(&heading, C2D_AlignCenter | C2D_WithColor, 200, 136, 0, .55f, .55f, white);
      C2D_DrawText(&status, C2D_AlignCenter | C2D_WithColor, 200, 170, 0, .43f, .43f, white);
      C2D_DrawText(&help, C2D_AlignCenter | C2D_WithColor, 200, 209, 0, .4f, .4f, white);
      C3D_FrameEnd(0);
    } else {
      gfxFlushBuffers();
      gfxSwapBuffers();
      gspWaitForVBlank();
    }
  }
  if (text_buffer) C2D_TextBufDelete(text_buffer);
  if (logo) C2D_SpriteSheetFree(logo);
  if (c2d_ready) C2D_Fini();
  if (c3d_ready) C3D_Fini();
  if (romfs_ready) romfsExit();
  ctr::shutdown_debug_console();
  gfxExit();
  return ready ? 0 : 1;
}

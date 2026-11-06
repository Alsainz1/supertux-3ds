#pragma once

namespace ctr {
// Call after gfxInitDefault(), before any engine or library initialization.
void init_debug_console();
void shutdown_debug_console();
// GPU rendering owns the top framebuffer. Flush only changed bottom pixels.
void flush_debug_console();
void flush_log();
void set_bottom_hud(bool enabled);
bool bottom_hud_enabled();
void log(const char* format, ...);
}

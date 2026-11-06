#pragma once
class DrawingContext;
class GameSession;
class TextObject;
class ScreenFade;
class DisplayEffect;
class Camera;
#include "math/vector.hpp"
#include "video/surface_ptr.hpp"
#include "video/font_ptr.hpp"
namespace ctr_hud {
void queue_fade(ScreenFade&);void queue_display_effect(DisplayEffect&);
bool drawing_effects();bool has_effects();void draw_effects();
void set_shake(const Camera*,const Vector&);Vector bottom_shake();
void queue_boss(const SurfacePtr&,int,int);
void begin_frame();void queue_text(TextObject&);bool drawing_text();bool drawing_map_bottom();
const FontPtr& menu_font();
void update();void invalidate();bool needs_redraw();void draw_bottom();
void prepare(GameSession&);void draw_top(DrawingContext&,GameSession&);void shutdown();
}

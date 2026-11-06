#pragma once
#include <memory>
#include <optional>
#include <string>
#include "math/rectf.hpp"
#include "video/color.hpp"
#include "video/blend.hpp"
#include "video/flip.hpp"
struct SDL_Surface;
class Viewport;
namespace ctr_gpu {
struct Texture;
void show_loading();
void init();
void shutdown();
void begin(const Viewport& viewport);
void present();
void wait_idle();
void clip(const Rect* rect);
void blend(const Blend& mode);
bool visible(const Rectf& destination, float angle);
bool allow_optional_warmup();
std::shared_ptr<Texture> upload(const SDL_Surface& image);
std::shared_ptr<Texture> load_cached(const std::string& filename, const std::optional<Rect>& rect);
void smooth_background(const Texture& texture);
void collect_unused();
void image(const Texture& texture, const Rectf& source, const Rectf& destination,
           float angle, Flip flip, const Color& color, float alpha);
void rectangle(const Rectf& rect, const Color& tl, const Color& tr,
               const Color& bl, const Color& br, float alpha = 1);
void line(const Vector& a, const Vector& b, const Color& color, float alpha);
void triangle(const Vector& a, const Vector& b, const Vector& c, const Color& color, float alpha);
}

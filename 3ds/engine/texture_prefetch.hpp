#pragma once
#include <memory>
class Texture;
namespace ctr_prefetch {
void enqueue(const std::shared_ptr<Texture>& texture, bool urgent = false);
void pump();
void clear();
}

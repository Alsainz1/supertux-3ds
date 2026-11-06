//  SuperTux
//  Copyright (C) 2015 Hume2 <teratux.mail@gmail.com>
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

#include "gui/item_label.hpp"
#ifdef __3DS__
#define LABEL_FONT get_font()
#else
#define LABEL_FONT Resources::big_font
#endif

#include "supertux/colorscheme.hpp"
#include "supertux/gameconfig.hpp"
#include "supertux/globals.hpp"
#include "supertux/resources.hpp"
#include "video/drawing_context.hpp"

ItemLabel::ItemLabel(const std::string& text_) :
  MenuItem(text_)
{
}

void
ItemLabel::draw(DrawingContext& context, const Vector& pos, int menu_width, bool active) {
  context.color().draw_text(LABEL_FONT, get_text(),
                            Vector(pos.x + static_cast<float>(menu_width) / 2.0f,
                                   pos.y - LABEL_FONT->get_height() / 2.0f ),
                            ALIGN_CENTER, LAYER_GUI, get_color());
}

Color
ItemLabel::get_color() const {
  return g_config->labeltextcolor;
}

int
ItemLabel::get_width() const {
  return static_cast<int>(LABEL_FONT->get_text_width(get_text())) + 16;
}

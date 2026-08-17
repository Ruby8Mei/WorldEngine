#pragma once

#include <string>

namespace inop {
namespace gui {

struct Color {
    float r, g, b, a;
};

inline Color rgba(float r, float g, float b, float a = 1.0f) { return Color{r, g, b, a}; }

bool render_init();
void render_shutdown();

void set_viewport(int width, int height);

void clear(Color background);

void draw_rect(float x, float y, float w, float h, Color c);
void draw_rect_outline(float x, float y, float w, float h, Color c, float thickness = 1.0f);

void begin_scissor(float x, float y, float w, float h);
void end_scissor();

enum class Font { Body, Wordmark, BodyLarge };

bool load_fonts();

float text_width(Font font, const std::string& text);
float text_line_height(Font font);
void draw_text(Font font, float x, float baseline_y, const std::string& text, Color c);

}
}


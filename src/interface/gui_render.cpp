#include "gui_render.hpp"

#include <map>

#include "gui_prefs.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// GLFW's header includes the platform GL header (GL/gl.h on Windows) for
// us, giving every OpenGL 1.1 entry point opengl32.dll exports statically —
// no GLAD/GLEW loader needed. This is only safe as long as gui.cpp never
// asks GLFW for a specific GL version/profile (see the comment there).
#include <GLFW/glfw3.h>

// GL_CLAMP_TO_EDGE is GL 1.2, not 1.1, so some gl.h headers omit the
// enum. It is just a constant, not a function pointer, so no loader is
// needed to use it — define it ourselves if missing.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace inop {
namespace gui {

namespace {

struct FontAtlas {
    GLuint texture = 0;
    std::vector<stbtt_bakedchar> chars;  // ASCII 32..127 (96 glyphs)
    int bitmap_w = 0, bitmap_h = 0;
    float pixel_height = 0;
    // Which of ASCII 32 to 126 the face has no glyph for. A face missing
    // one still bakes and still draws: stb puts the notdef glyph in the
    // slot, which comes out as an empty box. Recorded here so a caller
    // can ask before it draws, rather than an operator finding out from
    // a row of boxes.
    std::string missing;
};

FontAtlas g_body;
FontAtlas g_wordmark;
FontAtlas g_body_large;

// One preview atlas per typeface, keyed by the same filename the settings
// screen and font_path() use. Kept across a typeface change, unlike the
// three above: a preview says what a face looks like, which does not
// depend on which face the interface is currently wearing. Only shutdown
// empties it.
//
// A file that could not be baked is remembered as an empty atlas rather
// than retried, so a broken font in the folder costs one failed read and
// not one per frame the list is open.
std::map<std::string, FontAtlas> g_previews;

// Bumped by every successful bake, never reset. Read by anything that
// caches a width taken from the atlas, so a typeface change throws that
// cache away instead of drawing with measurements from the old face.
unsigned g_font_generation = 0;

bool read_file(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff len = f.tellg();
    if (len <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(len));
    f.read(reinterpret_cast<char*>(out.data()), len);
    return static_cast<bool>(f) || f.eof();
}

bool bake_font(const std::string& path, float pixel_height, FontAtlas& out, int side = 1024) {
    std::vector<unsigned char> ttf;
    if (!read_file(path, ttf)) {
        std::cerr << "gui: could not read font file '" << path << "'\n";
        return false;
    }
    const int bw = side, bh = side;
    std::vector<unsigned char> bitmap(static_cast<size_t>(bw) * bh);
    out.chars.resize(96);
    int result = stbtt_BakeFontBitmap(ttf.data(), 0, pixel_height, bitmap.data(), bw, bh, 32, 96,
                                       out.chars.data());
    if (result <= 0) {
        std::cerr << "gui: font atlas bake failed for '" << path << "'\n";
        return false;
    }
    // Asked of the font itself rather than of the baked bitmap. A notdef
    // glyph is not reliably blank, so counting empty pixels would call a
    // full stop missing and let a box through.
    out.missing.clear();
    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        for (int cp = 32; cp < 127; ++cp)
            if (stbtt_FindGlyphIndex(&info, cp) == 0)
                out.missing.push_back(static_cast<char>(cp));
    }

    glGenTextures(1, &out.texture);
    glBindTexture(GL_TEXTURE_2D, out.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, bw, bh, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.data());
    out.bitmap_w = bw;
    out.bitmap_h = bh;
    out.pixel_height = pixel_height;
    return true;
}

// Every atlas back to its unbaked state, textures released. Shared by
// shutdown and by a re-bake, since a re-bake that kept the old texture
// names would leak one atlas per typeface change.
void free_atlases() {
    if (g_body.texture) glDeleteTextures(1, &g_body.texture);
    if (g_wordmark.texture) glDeleteTextures(1, &g_wordmark.texture);
    if (g_body_large.texture) glDeleteTextures(1, &g_body_large.texture);
    g_body = FontAtlas{};
    g_wordmark = FontAtlas{};
    g_body_large = FontAtlas{};
}

const FontAtlas& atlas_for(Font font) {
    switch (font) {
        case Font::Wordmark:
            return g_wordmark;
        case Font::BodyLarge:
            return g_body_large;
        default:
            return g_body;
    }
}

// Both in pixels, not logical units: glScissor and glViewport speak
// pixels, and the width is kept so set_ui_scale() can redo the projection
// without waiting for the next resize event.
int g_viewport_w = 0;
int g_viewport_h = 0;  // needed to flip our top-left-origin rects into glScissor's bottom-left ones
float g_ui_scale = 1.0f;
// How far everything drawn is shifted, in logical units. Only ever moved
// by gui.cpp while one screen slides off and another slides on.
float g_offset_x = 0.0f;
float g_offset_y = 0.0f;

// One scissor rect in GL's bottom-left pixel space, ready for glScissor.
struct ScissorRect {
    int x, y, w, h;
};

// The scissor rects currently in force, innermost last. A stack rather
// than a single rect so that a clip inside a clip means both: a settings
// row that crops its own note is drawn inside the scroll region that crops
// the whole page, and the note must not escape either of them.
std::vector<ScissorRect> g_scissor_stack;

// Not every typeface draws the same point size at the same width. SGA is
// three times wider per letter than Courier at 18 pixels, which makes a
// panel laid out for Courier look shouted rather than typed, so it is
// baked smaller. Only faces that need it are listed; anything absent is
// baked at the size the layout was drawn for.
float typeface_size_scale(const std::string& font_file) {
    if (font_file == "sga-all-characters.otf") return 0.6f;
    return 1.0f;
}

void apply_top_scissor() {
    if (g_scissor_stack.empty()) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    const ScissorRect& r = g_scissor_stack.back();
    glEnable(GL_SCISSOR_TEST);
    glScissor(r.x, r.y, r.w, r.h);
}

}  // namespace

bool render_init() {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void render_shutdown() {
    free_atlases();
    for (std::pair<const std::string, FontAtlas>& e : g_previews)
        if (e.second.texture) glDeleteTextures(1, &e.second.texture);
    g_previews.clear();
}

void set_viewport(int width, int height) {
    if (width <= 0 || height <= 0) return;
    g_viewport_w = width;
    g_viewport_h = height;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Top-left origin, y increasing downward — matches GLFW cursor
    // coordinates so widget hit-testing needs no flip.
    //
    // Dividing the extents by the scale is the whole of the zoom: the
    // window still has the same pixels, but fewer logical units span it,
    // so everything drawn in logical units comes out proportionally
    // bigger. No widget, panel or layout constant knows this happened.
    glOrtho(0, static_cast<double>(width) / g_ui_scale,
            static_cast<double>(height) / g_ui_scale, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // Reapplied rather than dropped: a resize in the middle of a screen
    // transition would otherwise snap both halves back to the origin.
    glTranslatef(g_offset_x, g_offset_y, 0.0f);
}

void set_draw_offset(float dx, float dy) {
    g_offset_x = dx;
    g_offset_y = dy;
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(dx, dy, 0.0f);
}

void set_ui_scale(float scale) {
    if (scale <= 0.0f) return;
    g_ui_scale = scale;
    set_viewport(g_viewport_w, g_viewport_h);
}

float ui_scale() { return g_ui_scale; }

void begin_scissor(float x, float y, float w, float h) {
    // glScissor is bottom-left-origin regardless of the glOrtho we set up,
    // so flip y here rather than asking every caller to think in GL's
    // coordinate space. It also speaks pixels while callers speak logical
    // units, so the scale has to be undone on the way in.
    const float s = g_ui_scale;
    // The offset is part of where the caller actually is on screen, and
    // glScissor knows nothing about the modelview matrix, so it has to be
    // added by hand here.
    const float px = (x + g_offset_x) * s, py = (y + g_offset_y) * s;
    const float pw = w * s, ph = h * s;
    ScissorRect r;
    r.x = static_cast<int>(px);
    r.y = static_cast<int>(static_cast<float>(g_viewport_h) - (py + ph));
    r.w = static_cast<int>(pw);
    r.h = static_cast<int>(ph);

    // Nested, so the inner rect can only ever take space away. Overlapping
    // an outer rect from outside leaves a width or height of zero, which
    // draws nothing — which is right, since nothing of it was visible.
    if (!g_scissor_stack.empty()) {
        const ScissorRect& o = g_scissor_stack.back();
        const int x0 = r.x > o.x ? r.x : o.x;
        const int y0 = r.y > o.y ? r.y : o.y;
        const int x1r = r.x + r.w, x1o = o.x + o.w;
        const int y1r = r.y + r.h, y1o = o.y + o.h;
        const int x1 = x1r < x1o ? x1r : x1o;
        const int y1 = y1r < y1o ? y1r : y1o;
        r.x = x0;
        r.y = y0;
        r.w = x1 > x0 ? x1 - x0 : 0;
        r.h = y1 > y0 ? y1 - y0 : 0;
    }

    g_scissor_stack.push_back(r);
    apply_top_scissor();
}

void end_scissor() {
    if (!g_scissor_stack.empty()) g_scissor_stack.pop_back();
    apply_top_scissor();
}

void clear(Color background) {
    glClearColor(background.r, background.g, background.b, background.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void draw_rect(float x, float y, float w, float h, Color c) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

void draw_rect_outline(float x, float y, float w, float h, Color c, float thickness) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(c.r, c.g, c.b, c.a);
    glLineWidth(thickness);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

bool load_fonts(const std::string& font_file) {
    // Finding the file is gui_prefs work and not renderer work: it owns
    // the bundled folder and the system folder both, and the settings
    // screen filters its list with the same answer this bake asks for.
    const std::string path = font_path(font_file);
    if (path.empty()) {
        std::cerr << "gui: no font file named '" << font_file
                  << "' in fonts/ or the system font folder\n";
        return false;
    }
    // One typeface throughout, not just the wordmark — Body/BodyLarge used
    // to be Segoe UI, but the operator asked for one consistent typeface
    // across the whole panel. Which one it is became a preference; that it
    // is the same one in all three sizes did not.
    free_atlases();
    const float k = typeface_size_scale(font_file);
    bool ok_body = bake_font(path, 18.0f * k, g_body);
    bool ok_word = bake_font(path, 44.0f * k, g_wordmark);
    bool ok_large = bake_font(path, 28.0f * k, g_body_large);
    if (!(ok_body && ok_word && ok_large)) {
        free_atlases();
        return false;
    }
    ++g_font_generation;
    return true;
}

unsigned font_generation() { return g_font_generation; }

bool typeface_draws_latin(const std::string& font_file) {
    return font_file != "sga-all-characters.otf";
}

bool preview_font_ready(const std::string& font_file) {
    std::map<std::string, FontAtlas>::iterator it = g_previews.find(font_file);
    if (it != g_previews.end()) return it->second.texture != 0;

    // The entry goes in whatever happens, so a failure is remembered as a
    // failure and not asked again next frame.
    FontAtlas& a = g_previews[font_file];
    const std::string path = font_path(font_file);
    if (path.empty()) return false;
    // The same size the interface body text is baked at, and through the
    // same per-face reduction, so a face that has to be shrunk to sit in a
    // row is shrunk here too and the preview is honest about it.
    if (!bake_font(path, 18.0f * typeface_size_scale(font_file), a, 256)) {
        a = FontAtlas{};
        return false;
    }
    return true;
}

// File-local: FontAtlas is a private type and never leaves this file.
static const FontAtlas* preview_atlas(const std::string& font_file) {
    std::map<std::string, FontAtlas>::const_iterator it = g_previews.find(font_file);
    if (it == g_previews.end() || !it->second.texture) return nullptr;
    return &it->second;
}

float preview_text_width(const std::string& font_file, const std::string& text) {
    const FontAtlas* a = preview_atlas(font_file);
    if (!a) return 0.0f;
    float w = 0.0f;
    for (unsigned char ch : text) {
        if (ch < 32 || ch > 127) continue;
        w += a->chars[static_cast<size_t>(ch - 32)].xadvance;
    }
    return w;
}

bool typeface_can_spell(const std::string& font_file, const std::string& text) {
    // Two ways a face fails to spell something, and only one of them can
    // be found in the file. A rune face has every glyph and draws runes,
    // which is the named list. A face with a hole in its alphabet draws a
    // box, which is what the bake recorded.
    if (!typeface_draws_latin(font_file)) return false;
    const FontAtlas* a = preview_atlas(font_file);
    // Nothing baked to ask. Saying yes leaves the row as it was rather
    // than hanging a bracket off a name that never drew.
    if (!a) return true;
    for (char c : text)
        if (a->missing.find(c) != std::string::npos) return false;
    return true;
}

float preview_line_height(const std::string& font_file) {
    const FontAtlas* a = preview_atlas(font_file);
    return a ? a->pixel_height * 1.25f : 0.0f;
}

void draw_preview_text(const std::string& font_file, float x, float baseline_y,
                       const std::string& text, Color c) {
    const FontAtlas* a = preview_atlas(font_file);
    if (!a) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, a->texture);
    glColor4f(c.r, c.g, c.b, c.a);
    float xpos = x, ypos = baseline_y;
    glBegin(GL_QUADS);
    for (unsigned char ch : text) {
        if (ch < 32 || ch > 127) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(const_cast<stbtt_bakedchar*>(a->chars.data()), a->bitmap_w,
                           a->bitmap_h, ch - 32, &xpos, &ypos, &q, 1);
        glTexCoord2f(q.s0, q.t0);
        glVertex2f(q.x0, q.y0);
        glTexCoord2f(q.s1, q.t0);
        glVertex2f(q.x1, q.y0);
        glTexCoord2f(q.s1, q.t1);
        glVertex2f(q.x1, q.y1);
        glTexCoord2f(q.s0, q.t1);
        glVertex2f(q.x0, q.y1);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

float text_width(Font font, const std::string& text) {
    const FontAtlas& a = atlas_for(font);
    if (a.chars.empty()) return 0.0f;
    float w = 0.0f;
    for (unsigned char ch : text) {
        if (ch < 32 || ch > 127) continue;
        w += a.chars[static_cast<size_t>(ch - 32)].xadvance;
    }
    return w;
}

float text_line_height(Font font) { return atlas_for(font).pixel_height * 1.25f; }

void draw_text(Font font, float x, float baseline_y, const std::string& text, Color c) {
    const FontAtlas& a = atlas_for(font);
    if (!a.texture) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, a.texture);
    glColor4f(c.r, c.g, c.b, c.a);
    float xpos = x, ypos = baseline_y;
    glBegin(GL_QUADS);
    for (unsigned char ch : text) {
        if (ch < 32 || ch > 127) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(const_cast<stbtt_bakedchar*>(a.chars.data()), a.bitmap_w, a.bitmap_h,
                            ch - 32, &xpos, &ypos, &q, 1);
        glTexCoord2f(q.s0, q.t0);
        glVertex2f(q.x0, q.y0);
        glTexCoord2f(q.s1, q.t0);
        glVertex2f(q.x1, q.y0);
        glTexCoord2f(q.s1, q.t1);
        glVertex2f(q.x1, q.y1);
        glTexCoord2f(q.s0, q.t1);
        glVertex2f(q.x0, q.y1);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

}  // namespace gui
}  // namespace inop

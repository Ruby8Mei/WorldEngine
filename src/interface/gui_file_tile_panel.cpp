#include "gui_file_tile_panel.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gui_config_store.hpp"

namespace inop {
namespace gui {

namespace {
const float kBoxMargin = 60.0f;
const int kCols = 10;
const float kTileGap = 14.0f;
const float kHeaderH = 56.0f;
const float kCancelW = 90.0f, kCancelH = 32.0f;
const float kSectionHeaderH = 24.0f;

struct TileEntry {
    std::string label;
    std::string path;
    const PanelState* preset = nullptr;
};

struct Section {
    std::string header;
    std::vector<TileEntry> entries;
};

std::vector<Section> build_sections(TilePanelMode mode, const std::string& current_suite_code) {
    std::vector<Section> sections;

    if (mode == TilePanelMode::Load) {
        Section dev{"Developer setup", {}};
        for (const auto& p : developer_presets()) dev.entries.push_back(TileEntry{p.name, "", &p.state});
        sections.push_back(std::move(dev));
    }

    auto configs = list_configs();
    Section enigma{"Enigma setup", {}};
    Section inop{"INOP setup", {}};
    for (const auto& c : configs) {
        TileEntry e{c.filename, c.path, nullptr};
        if (c.suite_code == "26") enigma.entries.push_back(e);
        else inop.entries.push_back(e);
    }

    if (mode == TilePanelMode::Load) {
        sections.push_back(std::move(enigma));
        sections.push_back(std::move(inop));
    } else {
        sections.push_back(current_suite_code == "26" ? std::move(enigma) : std::move(inop));
    }
    return sections;
}

}

FileTilePanelResult file_tile_panel_frame(const GuiInput& in, const std::string& title,
                                           float viewport_w, float viewport_h, float& scroll,
                                           TilePanelMode mode, const std::string& current_suite_code) {
    FileTilePanelResult result;

    draw_rect(0, 0, viewport_w, viewport_h, rgba(0, 0, 0, 0.55f));

    Rect box{kBoxMargin, kBoxMargin, viewport_w - 2 * kBoxMargin, viewport_h - 2 * kBoxMargin};
    draw_rect(box.x, box.y, box.w, box.h, palette::panel());
    draw_rect_outline(box.x, box.y, box.w, box.h, palette::accent(), 2.0f);

    label(Rect{box.x + 20, box.y + 14, box.w - 200, kHeaderH}, title, false, Font::Wordmark);

    Rect cancel_r{box.x + box.w - kCancelW - 16, box.y + 12, kCancelW, kCancelH};
    if (button(cancel_r, "Cancel", in, true)) result.cancelled = true;

    Rect grid{box.x + 20, box.y + kHeaderH + 10, box.w - 40, box.h - kHeaderH - 30};
    float tile_size = (grid.w - kTileGap * (kCols - 1)) / static_cast<float>(kCols);
    float section_gap = tile_size + kTileGap;

    std::vector<Section> sections = build_sections(mode, current_suite_code);

    float content_h = 0.0f;
    for (size_t s = 0; s < sections.size(); ++s) {
        int rows = static_cast<int>(std::ceil(static_cast<double>(sections[s].entries.size()) / kCols));
        content_h += kSectionHeaderH + rows * (tile_size + kTileGap);
        if (s + 1 < sections.size()) content_h += section_gap;
    }
    float max_scroll = std::max(0.0f, content_h - grid.h);

    scroll -= static_cast<float>(in.scroll_y) * 24.0f;
    if (scroll < 0) scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;

    begin_scissor(grid.x, grid.y, grid.w, grid.h);
    float cursor_y = grid.y - scroll;
    for (size_t s = 0; s < sections.size(); ++s) {
        const Section& sec = sections[s];
        if (cursor_y + kSectionHeaderH >= grid.y && cursor_y <= grid.y + grid.h)
            label(Rect{grid.x, cursor_y, grid.w, kSectionHeaderH}, sec.header, true);
        cursor_y += kSectionHeaderH;

        for (size_t i = 0; i < sec.entries.size(); ++i) {
            int col = static_cast<int>(i) % kCols;
            int row = static_cast<int>(i) / kCols;
            float tx = grid.x + col * (tile_size + kTileGap);
            float ty = cursor_y + row * (tile_size + kTileGap);
            if (ty + tile_size < grid.y || ty > grid.y + grid.h) continue;

            const TileEntry& entry = sec.entries[i];
            Rect tile{tx, ty, tile_size, tile_size};
            bool hovered = rect_contains(tile, in.mouse_x, in.mouse_y) &&
                           rect_contains(grid, in.mouse_x, in.mouse_y);
            draw_rect(tile.x, tile.y, tile.w, tile.h, hovered ? palette::border() : palette::disabled_bg());
            draw_rect_outline(tile.x, tile.y, tile.w, tile.h, palette::border());
            label(Rect{tile.x + 4, tile.y + tile.h - 22, tile.w - 8, 20}, entry.label, true);

            if (entry.preset) {
                if (hovered && in.mouse_pressed) {
                    result.preset_picked = true;
                    result.preset = entry.preset;
                }
                continue;
            }

            Rect delete_r{tile.x + tile.w - 22, tile.y + 4, 18, 18};
            if (button(delete_r, "x", in, true)) {
                result.delete_requested = true;
                result.delete_path = entry.path;
            } else if (hovered && in.mouse_pressed) {
                result.picked = true;
                result.path = entry.path;
            }
        }

        int rows = static_cast<int>(std::ceil(static_cast<double>(sec.entries.size()) / kCols));
        cursor_y += rows * (tile_size + kTileGap);
        if (s + 1 < sections.size()) cursor_y += section_gap;
    }
    end_scissor();

    if (in.mouse_pressed && !result.picked && !result.preset_picked && !result.cancelled &&
        !rect_contains(box, in.mouse_x, in.mouse_y)) {
        result.cancelled = true;
    }

    return result;
}

}
}


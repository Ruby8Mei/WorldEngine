#include "gui_bombe_panel.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "gui_form.hpp"
#include "gui_render.hpp"
#include "registry.hpp"

namespace inop {
namespace gui {
namespace {

constexpr float kMargin = 16.0f;
constexpr float kGap = 8.0f;
constexpr float kButtonW = 230.0f;
constexpr float kButtonH = 30.0f;
constexpr float kResultH = 190.0f;

}

BombePanel::BombePanel() {
  total_.store(bombe::approved_batch_positions());
  total_orders_.store(bombe::approved_rotor_orders().size());
  status_ = "Ready for a historic Bombe batch.";
}

BombePanel::~BombePanel() {
  cancel();
  if (worker_.joinable())
    worker_.join();
}

void BombePanel::self_test(
    const std::function<void(bool, const std::string &)> &check) {
  BombePanel panel;
  panel.set_immediate_result(bombe::SearchState::Failure, "Synthetic failure.");
  check(panel.result_text_.find("FAILURE") != std::string::npos &&
            panel.result_text_.find("Synthetic failure.") != std::string::npos,
        "Bombe panel presents an internal failure state");
  panel.set_immediate_result(bombe::SearchState::NoResult,
                             "Synthetic no result.");
  check(panel.result_text_.find("NO RESULT") != std::string::npos,
        "Bombe panel presents a no-result state");
  panel.set_immediate_result(bombe::SearchState::InvalidInput,
                             "Synthetic invalid input.");
  check(panel.result_text_.find("INVALID INPUT") != std::string::npos,
        "Bombe panel presents an invalid-input state");
  panel.result_ = {};
  panel.result_.state = bombe::SearchState::Cancelled;
  panel.result_.tested = 17576;
  panel.result_.total = bombe::approved_batch_positions();
  panel.result_.completed_orders = 1;
  panel.result_.total_orders = bombe::approved_rotor_orders().size();
  panel.rebuild_result_text();
  check(panel.result_text_.find("1 of 60 rotor orders") != std::string::npos,
        "Bombe panel presents batched cancellation progress");
}

void BombePanel::open() {
  cancel();
  if (worker_.joinable())
    worker_.join();
  running_.store(false);
  finished_.store(false);
  cancel_requested_.store(false);
  open_dropdown_id_ = -1;
  wordmark_clicked_ = false;
}

void BombePanel::cancel() {
  cancel_requested_.store(true);
  if (running_.load())
    status_ = "Cancellation requested. Finishing the current safe step...";
}

void BombePanel::load_demonstration() {
  const bombe::SearchSpec spec = bombe::demonstration_spec();
  ciphertext_ = spec.ciphertext;
  crib_ = spec.crib;
  offset_ = std::to_string(spec.crib_offset);
  status_ = "Public steckered batch demonstration loaded.";
  result_text_.clear();
  has_result_ = false;
  result_scroll_ = 0.0f;
}

void BombePanel::set_immediate_result(bombe::SearchState state,
                                      const std::string &message) {
  result_ = {};
  result_.state = state;
  result_.message = message;
  result_.total = bombe::approved_batch_positions();
  result_.total_orders = bombe::approved_rotor_orders().size();
  has_result_ = true;
  status_ = message;
  rebuild_result_text();
}

void BombePanel::start_search() {
  if (running_.load())
    return;
  if (worker_.joinable())
    worker_.join();

  bombe::SearchSpec spec;
  spec.ciphertext = ciphertext_;
  spec.crib = crib_;
  spec.reflector = "B";
  spec.menu_limit = 12;
  spec.result_limit = 64;

  try {
    std::size_t used = 0;
    const unsigned long long parsed = std::stoull(offset_, &used);
    if (used != offset_.size() || parsed > 255)
      throw std::invalid_argument("offset");
    spec.crib_offset = static_cast<std::size_t>(parsed);
  } catch (...) {
    set_immediate_result(bombe::SearchState::InvalidInput,
                         "Offset must be a whole number from 0 to 255.");
    return;
  }

  cancel_requested_.store(false);
  running_.store(true);
  finished_.store(false);
  tested_.store(0);
  total_.store(bombe::approved_batch_positions());
  stops_.store(0);
  order_index_.store(1);
  completed_orders_.store(0);
  total_orders_.store(bombe::approved_rotor_orders().size());
  order_tested_.store(0);
  order_total_.store(17576);
  has_result_ = false;
  result_text_.clear();
  result_scroll_ = 0.0f;
  status_ = "Running the approved rotor order batch...";

  worker_ = std::thread([this, spec = std::move(spec)] {
    const bombe::BatchLimits limits{16, 64};
    bombe::SearchResult value = bombe::search_batch(
        spec, limits, [this] { return cancel_requested_.load(); },
        [this](const bombe::SearchProgress &progress) {
          tested_.store(progress.tested);
          total_.store(progress.total);
          stops_.store(progress.stops);
          order_index_.store(progress.order_index);
          completed_orders_.store(progress.completed_orders);
          total_orders_.store(progress.total_orders);
          order_tested_.store(progress.order_tested);
          order_total_.store(progress.order_total);
        });
    {
      std::lock_guard<std::mutex> lock(result_mutex_);
      result_ = std::move(value);
    }
    running_.store(false);
    finished_.store(true);
  });
}

void BombePanel::collect_finished() {
  if (!finished_.exchange(false))
    return;
  if (worker_.joinable())
    worker_.join();
  {
    std::lock_guard<std::mutex> lock(result_mutex_);
    tested_.store(result_.tested);
    total_.store(result_.total);
    stops_.store(result_.stop_count);
    completed_orders_.store(result_.completed_orders);
    total_orders_.store(result_.total_orders);
    if (result_.state == bombe::SearchState::Cancelled &&
        result_.order_summaries.size() > result_.completed_orders) {
      order_index_.store(result_.completed_orders + 1);
      order_tested_.store(result_.order_summaries.back().tested);
      order_total_.store(17576);
    } else {
      order_index_.store(0);
      order_tested_.store(0);
    }
    status_ = result_.message;
    has_result_ = true;
  }
  rebuild_result_text();
}

void BombePanel::rebuild_result_text() {
  std::ostringstream out;
  switch (result_.state) {
  case bombe::SearchState::Success:
    out << "SUCCESS\n" << result_.stop_count << " stop";
    if (result_.stop_count != 1)
      out << "s";
    out << " survived across " << result_.completed_orders << " rotor orders.\n"
        << "Partial stecker assignments only. Check every stop on an Enigma "
           "checking machine.\n\n";
    for (std::size_t i = 0; i < result_.stops.size() && i < 16; ++i)
      out << bombe::format_stop(result_.stops[i]) << "\n";
    if (result_.stops.size() > 16 || result_.stops_truncated)
      out << "More stops were counted beyond the panel or batch retention "
             "bounds.\n";
    break;
  case bombe::SearchState::NoResult:
    out << "NO RESULT\nNo rotor core position survived this menu across all "
        << result_.completed_orders << " approved rotor orders.";
    break;
  case bombe::SearchState::InvalidInput:
    out << "INVALID INPUT\n" << result_.message;
    break;
  case bombe::SearchState::Cancelled:
    out << "CANCELLED\nTested " << result_.tested << " of " << result_.total
        << " rotor core positions. Completed " << result_.completed_orders
        << " of " << result_.total_orders << " rotor orders.";
    break;
  case bombe::SearchState::Failure:
    out << "FAILURE\n" << result_.message;
    break;
  }
  result_text_ = out.str();
}

void BombePanel::frame(const GuiInput &in, int width, int height) {
  collect_finished();
  wordmark_clicked_ = false;
  const float w = static_cast<float>(width);
  const float h = static_cast<float>(height);
  begin_widget_frame();

  const float top = form_screen_header(in, w, "Bombe", &wordmark_clicked_);
  FormMetrics metrics;
  metrics.col_w = form_col_w(w);
  metrics.label_w = 170.0f;
  metrics.ctrl_w = 390.0f;
  metrics.gap = 16.0f;
  const float x = form_col_x(w);
  const float start = begin_scroll_region(top, w, h, scroll_, content_h_, in);
  float y = start;

  label(Rect{x, y, metrics.col_w, 30.0f},
        "One batched Turing and Welchman Bombe run. Menu, scramblers, "
        "diagonal board, stops.",
        true);
  y += 38.0f;
  y = form_heading(metrics, x, y, "Machine basis");

  std::string rotor_pool = "I  II  III  IV  V";
  form_row_label(metrics, x, y, "Approved rotor pool");
  text_field(form_control_rect(metrics, x, y), rotor_pool, in,
             "IVX ", 18, false, false, CaseFold::ToUpper);
  form_row_note(metrics, x, y, "60 distinct three rotor orders in pool order");
  y += kFormRowH + kFormRowGap;

  std::string reflector = "B";
  form_row_label(metrics, x, y, "Reflector");
  text_field(form_control_rect(metrics, x, y), reflector, in, "B", 1, false,
             false, CaseFold::ToUpper, "", true);
  form_row_note(metrics, x, y, "Reflector B is the supported historic stage");
  y += kFormRowH + kFormRowGap;

  std::string basis = "AAA / 10 STECKER PAIRS";
  form_row_label(metrics, x, y, "Ring and board basis");
  text_field(form_control_rect(metrics, x, y), basis, in,
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ /", 22, false, false,
             CaseFold::ToUpper);
  form_row_note(metrics, x, y, "Stops contain partial stecker implications");
  y += kFormRowH + kFormSectionGap;

  y = form_heading(metrics, x, y, "Crib menu");
  const float field_h = text_field_height(2, true);
  form_row_label(metrics, x, y, "Ciphertext");
  text_field(
      Rect{form_control_rect(metrics, x, y).x, y, metrics.ctrl_w, field_h},
      ciphertext_, in, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 256, !running_.load(),
      false, CaseFold::ToUpper, "A TO Z, MAX 256", false, 2, "CIPHERTEXT");
  form_row_note(metrics, x, y, "Aligned ciphertext for the known crib");
  y += field_h + kFormRowGap;

  form_row_label(metrics, x, y, "Crib");
  text_field(
      Rect{form_control_rect(metrics, x, y).x, y, metrics.ctrl_w, field_h},
      crib_, in, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 64, !running_.load(), false,
      CaseFold::ToUpper, "A TO Z, 6 TO 64", false, 2, "KNOWN PLAINTEXT");
  form_row_note(metrics, x, y,
                "The strongest connected 6 to 12 link menu is used");
  y += field_h + kFormRowGap;

  form_row_label(metrics, x, y, "Crib offset");
  numeric_field(form_control_rect(metrics, x, y), offset_, in, 3,
                !running_.load(), false);
  form_row_note(metrics, x, y, "Zero based position in the ciphertext");
  y += kFormRowH + kFormRowGap;

  const bool running = running_.load();
  if (button(Rect{x, y, kButtonW, kButtonH}, "Load public demonstration", in,
             !running))
    load_demonstration();
  if (button(Rect{x + kButtonW + kGap, y, kButtonW, kButtonH},
             running ? "Cancel run" : "Run Bombe", in, true, running)) {
    if (running)
      cancel();
    else
      start_search();
  }
  y += kButtonH + kFormRowGap;

  label(Rect{x, y, metrics.col_w, 26.0f},
        "Estimated work: 60 orders and 1,054,560 rotor core positions.", true);
  y += 30.0f;

  const std::uint64_t tested = tested_.load();
  const std::uint64_t total = std::max<std::uint64_t>(1, total_.load());
  const float fraction = std::clamp(
      static_cast<float>(tested) / static_cast<float>(total), 0.0f, 1.0f);
  draw_rect(x, y, metrics.col_w, 12.0f, palette::disabled_bg());
  draw_rect(x, y, metrics.col_w * fraction, 12.0f, palette::accent());
  y += 16.0f;
  std::ostringstream progress;
  progress << status_ << "   Aggregate " << tested << "/" << total
           << " positions   " << stops_.load() << " stops";
  label(Rect{x, y, metrics.col_w, 26.0f}, progress.str(), true);
  y += 28.0f;
  const std::size_t current_index = order_index_.load();
  const std::vector<std::vector<std::string>> &orders =
      bombe::approved_rotor_orders();
  std::ostringstream order_progress;
  if (current_index > 0 && current_index <= orders.size()) {
    order_progress << "Current order " << current_index << "/"
                   << total_orders_.load() << ": ";
    for (const std::string &name : orders[current_index - 1])
      order_progress << name << " ";
    order_progress << "  " << order_tested_.load() << "/"
                   << order_total_.load() << " positions";
  } else {
    order_progress << "Completed orders " << completed_orders_.load() << "/"
                   << total_orders_.load();
  }
  label(Rect{x, y, metrics.col_w, 26.0f}, order_progress.str(), true);
  y += 32.0f;

  const std::string shown =
      has_result_ ? result_text_
                  : "Stops are candidates, not completed keys. Results shown "
                    "here are sensitive key material.";
  text_block(Rect{x, y, metrics.col_w, kResultH}, shown, in, result_scroll_,
             !has_result_, "BOMBE OUTPUT");
  y += kResultH + kFormRowGap;
  label(Rect{x, y, metrics.col_w, 28.0f},
        "Not implemented: checking machine, Naval Enigma, INOP-38.",
        true);
  y += 36.0f;

  content_h_ = y + kMargin - start;
  end_scroll_region(top, w, h, scroll_, content_h_);
  draw_open_dropdown_popup(in, open_dropdown_id_);
  end_widget_frame(in);
}

}
}

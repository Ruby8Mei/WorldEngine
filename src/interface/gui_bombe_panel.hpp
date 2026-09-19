#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "bombe_engine.hpp"
#include "gui_widgets.hpp"

namespace inop {
namespace gui {

class BombePanel {
public:
  BombePanel();
  ~BombePanel();
  static void
  self_test(const std::function<void(bool, const std::string &)> &check);

  void open();
  void cancel();
  void frame(const GuiInput &in, int width, int height);
  bool wordmark_clicked() const { return wordmark_clicked_; }

private:
  void load_demonstration();
  void start_search();
  void collect_finished();
  void set_immediate_result(bombe::SearchState state,
                            const std::string &message);
  void rebuild_result_text();

  std::string ciphertext_;
  std::string crib_;
  std::string offset_ = "0";
  int open_dropdown_id_ = -1;
  float scroll_ = 0.0f;
  float content_h_ = 720.0f;
  float result_scroll_ = 0.0f;

  std::thread worker_;
  std::atomic<bool> cancel_requested_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> finished_{false};
  std::atomic<std::uint64_t> tested_{0};
  std::atomic<std::uint64_t> total_{1054560};
  std::atomic<std::uint64_t> stops_{0};
  std::atomic<std::size_t> order_index_{0};
  std::atomic<std::size_t> completed_orders_{0};
  std::atomic<std::size_t> total_orders_{60};
  std::atomic<std::uint64_t> order_tested_{0};
  std::atomic<std::uint64_t> order_total_{17576};
  std::mutex result_mutex_;
  bombe::SearchResult result_;
  std::string result_text_;
  std::string status_ = "Ready for a historic Bombe batch.";
  bool has_result_ = false;
  bool wordmark_clicked_ = false;
};

}
}

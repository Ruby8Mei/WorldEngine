#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace inop {
namespace bombe {

enum class SearchState { Success, NoResult, InvalidInput, Cancelled, Failure };

struct SearchSpec {
  std::string ciphertext;
  std::string crib;
  std::size_t crib_offset = 0;
  std::vector<std::string> rotor_order{"I", "II", "III"};
  std::string reflector = "B";
  std::size_t menu_limit = 12;
  std::size_t result_limit = 64;
};

struct MenuLink {
  char plain = 'A';
  char cipher = 'A';
  std::size_t position = 0;
};

struct Stop {
  std::vector<std::string> rotor_order;
  std::string rotor_core;
  char test_letter = 'A';
  char test_partner = 'A';
  std::vector<std::string> implied_pairs;
  std::vector<char> implied_self;
};

struct SearchProgress {
  std::uint64_t tested = 0;
  std::uint64_t total = 0;
  std::uint64_t stops = 0;
  std::vector<std::string> rotor_order;
  std::size_t order_index = 0;
  std::size_t completed_orders = 0;
  std::size_t total_orders = 0;
  std::uint64_t order_tested = 0;
  std::uint64_t order_total = 0;
};

struct OrderSummary {
  std::vector<std::string> rotor_order;
  std::uint64_t tested = 0;
  std::uint64_t stop_count = 0;
  std::size_t retained_stop_count = 0;
  bool stops_truncated = false;
};

struct SearchResult {
  SearchState state = SearchState::Failure;
  std::string message;
  std::vector<MenuLink> menu;
  std::vector<Stop> stops;
  std::uint64_t tested = 0;
  std::uint64_t total = 0;
  std::uint64_t stop_count = 0;
  bool stops_truncated = false;
  std::size_t completed_orders = 0;
  std::size_t total_orders = 0;
  std::vector<OrderSummary> order_summaries;
  double seconds = 0.0;
};

struct BatchLimits {
  std::size_t retained_stops_per_order = 16;
  std::size_t retained_stops_total = 64;
};

using CancelCheck = std::function<bool()>;
using ProgressSink = std::function<void(const SearchProgress &)>;

SearchResult search(const SearchSpec &spec, const CancelCheck &cancelled = {},
                    const ProgressSink &progress = {});
SearchResult search_batch(const SearchSpec &spec,
                          const BatchLimits &limits = {},
                          const CancelCheck &cancelled = {},
                          const ProgressSink &progress = {});
const std::vector<std::string> &approved_rotor_pool();
const std::vector<std::vector<std::string>> &approved_rotor_orders();
std::uint64_t approved_batch_positions();
SearchSpec demonstration_spec();
std::string format_stop(const Stop &stop);
void self_test(const std::function<void(bool, const std::string &)> &check);

}
}

#include "bombe_engine.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace inop {
namespace bombe {
namespace {

constexpr std::uint64_t kCorePositions = 26ULL * 26ULL * 26ULL;

char plug_partner(char letter, const std::vector<std::string> &pairs) {
  for (const std::string &pair : pairs) {
    if (pair[0] == letter)
      return pair[1];
    if (pair[1] == letter)
      return pair[0];
  }
  return letter;
}

std::string serialized_stops(const std::vector<Stop> &stops) {
  std::ostringstream out;
  for (const Stop &stop : stops)
    out << format_stop(stop) << "\n";
  return out.str();
}

}

void self_test(const std::function<void(bool, const std::string &)> &check) {
  const SearchSpec fixture = demonstration_spec();
  std::uint64_t last_progress = 0;
  bool monotonic = true;
  SearchResult recovered =
      search(fixture, {}, [&](const SearchProgress &value) {
        if (value.tested < last_progress || value.tested > value.total)
          monotonic = false;
        last_progress = value.tested;
      });
  const std::vector<std::string> plugs{"AD", "ET", "HM", "JL", "NV",
                                       "FU", "GQ", "PZ", "OX", "IK"};
  bool found_truth = false;
  for (const Stop &stop : recovered.stops)
    if (stop.rotor_core == "DKX" &&
        stop.test_partner == plug_partner(stop.test_letter, plugs))
      found_truth = true;
  check(recovered.state == SearchState::Success &&
            recovered.tested == kCorePositions && found_truth,
        "historic Bombe retains the true steckered Legacy stop");
  check(recovered.checked_stop_count == 1 &&
            recovered.checked_stops.size() == 1 &&
            recovered.checked_stops.front().completed_pairs ==
                std::vector<std::string>{"AD", "ET", "FU", "GQ", "HM",
                                         "IK", "JL", "NV", "OX", "PZ"},
        "checking machine completes the true ten pair stecker setting");
  check(monotonic && last_progress == kCorePositions,
        "historic Bombe progress is monotonic and reaches the full run size");
  check(recovered.menu.size() >= 6 && recovered.menu.size() <= 12,
        "historic Bombe builds a bounded connected menu");

  const BatchLimits matching_limits{fixture.result_limit,
                                    fixture.result_limit};
  SearchResult one_order_batch =
      search_order_batch(fixture, {fixture.rotor_order}, matching_limits, {}, {});
  check(one_order_batch.state == recovered.state &&
            one_order_batch.tested == recovered.tested &&
            one_order_batch.stop_count == recovered.stop_count &&
            serialized_stops(one_order_batch.stops) ==
                serialized_stops(recovered.stops),
        "one order batch matches the single order engine byte for byte");

  SearchSpec wrong = fixture;
  wrong.rotor_order = {"I", "II", "III"};
  SearchResult wrong_result = search(wrong);
  bool wrong_truth = false;
  for (const Stop &stop : wrong_result.stops)
    if (stop.rotor_core == "DKX" &&
        stop.test_partner == plug_partner(stop.test_letter, plugs))
      wrong_truth = true;
  check(!wrong_truth, "a wrong rotor order does not reproduce the true stop");

  const std::vector<std::vector<std::string>> no_result_orders{
      {"I", "II", "III"}, {"I", "II", "IV"}};
  SearchResult no_result =
      search_order_batch(fixture, no_result_orders, BatchLimits{}, {}, {});
  check(no_result.state == SearchState::NoResult &&
            no_result.completed_orders == no_result_orders.size(),
        "historic Bombe batch reports a complete no result run");

  std::size_t cancel_checks = 0;
  SearchResult cancelled =
      search(fixture, [&] { return ++cancel_checks > 16; });
  check(cancelled.state == SearchState::Cancelled &&
            cancelled.tested < cancelled.total,
        "historic Bombe cancellation stops a partial run");

  bool cancel_between = false;
  SearchResult between = search_order_batch(
      fixture, no_result_orders, BatchLimits{},
      [&] { return cancel_between; }, [&](const SearchProgress &value) {
        if (value.completed_orders == 1)
          cancel_between = true;
      });
  check(between.state == SearchState::Cancelled &&
            between.completed_orders == 1 &&
            between.tested == kCorePositions,
        "historic Bombe batch cancels between rotor orders");

  std::size_t batch_cancel_checks = 0;
  SearchResult within = search_order_batch(
      fixture, no_result_orders, BatchLimits{},
      [&] { return ++batch_cancel_checks > 16; }, {});
  check(within.state == SearchState::Cancelled &&
            within.completed_orders == 0 && within.tested < kCorePositions,
        "historic Bombe batch cancels within a rotor order");

  BatchLimits bounded_limits;
  bounded_limits.retained_stops_per_order = 1;
  bounded_limits.retained_stops_total = 1;
  SearchResult bounded = search_order_batch(
      fixture, {fixture.rotor_order, fixture.rotor_order}, bounded_limits, {},
      {});
  check(bounded.state == SearchState::Success && bounded.stop_count == 2 &&
            bounded.stops.size() == 1 && bounded.stops_truncated,
        "historic Bombe batch counts stops beyond both retention bounds");

  SearchResult complete = search_batch(fixture);
  bool complete_ordering = complete.order_summaries.size() ==
                           approved_rotor_orders().size();
  for (std::size_t i = 0;
       complete_ordering && i < complete.order_summaries.size(); ++i)
    complete_ordering = complete.order_summaries[i].rotor_order ==
                        approved_rotor_orders()[i];
  check(complete.state == SearchState::Success &&
            complete.completed_orders == approved_rotor_orders().size() &&
            complete.tested == approved_batch_positions() && complete_ordering,
        "historic Bombe batch enumerates every approved order once in order");
  std::size_t checked_raw = 0;
  std::size_t rejected_raw = 0;
  for (const Stop &stop : complete.stops) {
    if (stop.check_state == StopCheckState::Checked)
      ++checked_raw;
    if (stop.check_state == StopCheckState::Rejected)
      ++rejected_raw;
  }
  check(complete.stop_count == 14 && complete.checked_stop_count == 1 &&
            complete.rejected_stop_count == 13 && checked_raw == 1 &&
            rejected_raw == 13 && complete.checked_stops.size() == 1,
        "checking machine rejects deliberately false raw stops without changing raw counts");

  SearchSpec crashed = fixture;
  crashed.crib = "AAAAAA";
  crashed.ciphertext = "AAAAAA";
  SearchResult invalid = search(crashed);
  check(invalid.state == SearchState::InvalidInput && invalid.tested == 0,
        "historic Bombe refuses a crashed crib alignment");
}

}
}

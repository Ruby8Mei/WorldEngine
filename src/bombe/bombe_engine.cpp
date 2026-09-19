#include "bombe_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "inop.hpp"
#include "registry.hpp"

namespace inop {
namespace bombe {
namespace {

constexpr int kAlphabetSize = 26;
constexpr std::uint64_t kCorePositions = 26ULL * 26ULL * 26ULL;

struct MenuBuild {
  bool ok = false;
  std::string message;
  std::vector<MenuLink> links;
  int test_letter = 0;
};

struct DisjointSet {
  std::array<int, kAlphabetSize> parent{};

  DisjointSet() {
    for (int i = 0; i < kAlphabetSize; ++i)
      parent[static_cast<std::size_t>(i)] = i;
  }

  int root(int value) {
    int &parent_value = parent[static_cast<std::size_t>(value)];
    if (parent_value != value)
      parent_value = root(parent_value);
    return parent_value;
  }

  void join(int a, int b) {
    a = root(a);
    b = root(b);
    if (a != b)
      parent[static_cast<std::size_t>(b)] = a;
  }
};

int letter_index(char value) { return static_cast<int>(value - 'A'); }

bool legacy_text(const std::string &value) {
  return std::all_of(value.begin(), value.end(),
                     [](char c) { return c >= 'A' && c <= 'Z'; });
}

MenuBuild build_menu(const SearchSpec &spec) {
  MenuBuild out;
  if (spec.ciphertext.empty()) {
    out.message = "Ciphertext is required.";
    return out;
  }
  if (spec.crib.size() < 6) {
    out.message = "The crib must contain at least 6 letters.";
    return out;
  }
  if (spec.ciphertext.size() > 256 || spec.crib.size() > 64) {
    out.message =
        "Ciphertext is limited to 256 letters and the crib to 64 letters.";
    return out;
  }
  if (!legacy_text(spec.ciphertext) || !legacy_text(spec.crib)) {
    out.message = "Ciphertext and crib must contain only A to Z.";
    return out;
  }
  if (spec.crib_offset > spec.ciphertext.size() ||
      spec.crib.size() > spec.ciphertext.size() - spec.crib_offset) {
    out.message = "The crib extends past the ciphertext at this offset.";
    return out;
  }
  if (spec.menu_limit < 6 || spec.menu_limit > 12) {
    out.message = "The menu limit must be from 6 to 12 links.";
    return out;
  }

  std::vector<MenuLink> all;
  all.reserve(spec.crib.size());
  DisjointSet sets;
  for (std::size_t i = 0; i < spec.crib.size(); ++i) {
    const char plain = spec.crib[i];
    const char cipher = spec.ciphertext[spec.crib_offset + i];
    if (plain == cipher) {
      out.message = "The crib crashes against the ciphertext at position " +
                    std::to_string(spec.crib_offset + i + 1) + ".";
      return out;
    }
    all.push_back(MenuLink{plain, cipher, spec.crib_offset + i});
    sets.join(letter_index(plain), letter_index(cipher));
  }

  std::array<int, kAlphabetSize> edge_count{};
  std::array<int, kAlphabetSize> vertex_count{};
  std::array<bool, kAlphabetSize> seen_vertex{};
  for (const MenuLink &link : all)
    ++edge_count[static_cast<std::size_t>(sets.root(letter_index(link.plain)))];
  for (int i = 0; i < kAlphabetSize; ++i) {
    const int root = sets.root(i);
    if (edge_count[static_cast<std::size_t>(root)] > 0 &&
        !seen_vertex[static_cast<std::size_t>(i)]) {
      seen_vertex[static_cast<std::size_t>(i)] = true;
      ++vertex_count[static_cast<std::size_t>(root)];
    }
  }

  int selected_root = -1;
  for (int i = 0; i < kAlphabetSize; ++i) {
    if (selected_root < 0 ||
        edge_count[static_cast<std::size_t>(i)] >
            edge_count[static_cast<std::size_t>(selected_root)] ||
        (edge_count[static_cast<std::size_t>(i)] ==
             edge_count[static_cast<std::size_t>(selected_root)] &&
         vertex_count[static_cast<std::size_t>(i)] >
             vertex_count[static_cast<std::size_t>(selected_root)]))
      selected_root = i;
  }

  std::vector<MenuLink> component;
  for (const MenuLink &link : all)
    if (sets.root(letter_index(link.plain)) == selected_root)
      component.push_back(link);
  if (component.size() < 6) {
    out.message = "The strongest connected menu has fewer than 6 links.";
    return out;
  }

  std::array<int, kAlphabetSize> component_degree{};
  for (const MenuLink &link : component) {
    ++component_degree[static_cast<std::size_t>(letter_index(link.plain))];
    ++component_degree[static_cast<std::size_t>(letter_index(link.cipher))];
  }
  std::size_t first = 0;
  int first_score = -1;
  for (std::size_t i = 0; i < component.size(); ++i) {
    const int score = component_degree[static_cast<std::size_t>(
                          letter_index(component[i].plain))] +
                      component_degree[static_cast<std::size_t>(
                          letter_index(component[i].cipher))];
    if (score > first_score) {
      first = i;
      first_score = score;
    }
  }

  std::vector<bool> picked(component.size(), false);
  std::array<bool, kAlphabetSize> selected_vertices{};
  auto pick = [&](std::size_t index) {
    picked[index] = true;
    out.links.push_back(component[index]);
    selected_vertices[static_cast<std::size_t>(
        letter_index(component[index].plain))] = true;
    selected_vertices[static_cast<std::size_t>(
        letter_index(component[index].cipher))] = true;
  };
  pick(first);
  while (out.links.size() < spec.menu_limit &&
         out.links.size() < component.size()) {
    std::size_t choice = component.size();
    int choice_rank = -1;
    int choice_score = -1;
    for (std::size_t i = 0; i < component.size(); ++i) {
      if (picked[i])
        continue;
      const bool a = selected_vertices[static_cast<std::size_t>(
          letter_index(component[i].plain))];
      const bool b = selected_vertices[static_cast<std::size_t>(
          letter_index(component[i].cipher))];
      if (!a && !b)
        continue;
      const int rank = a && b ? 2 : 1;
      const int score = component_degree[static_cast<std::size_t>(
                            letter_index(component[i].plain))] +
                        component_degree[static_cast<std::size_t>(
                            letter_index(component[i].cipher))];
      if (rank > choice_rank || (rank == choice_rank && score > choice_score)) {
        choice = i;
        choice_rank = rank;
        choice_score = score;
      }
    }
    if (choice == component.size())
      break;
    pick(choice);
  }

  std::array<int, kAlphabetSize> degree{};
  std::size_t vertices = 0;
  for (const MenuLink &link : out.links) {
    ++degree[static_cast<std::size_t>(letter_index(link.plain))];
    ++degree[static_cast<std::size_t>(letter_index(link.cipher))];
  }
  for (int value : degree)
    if (value > 0)
      ++vertices;
  if (out.links.size() < vertices) {
    out.message =
        "The selected menu has no loop. Choose a stronger crib alignment.";
    out.links.clear();
    return out;
  }

  out.test_letter = static_cast<int>(std::distance(
      degree.begin(), std::max_element(degree.begin(), degree.end())));
  std::sort(out.links.begin(), out.links.end(),
            [](const MenuLink &a, const MenuLink &b) {
              return a.position < b.position;
            });
  out.ok = true;
  out.message = "Menu ready.";
  return out;
}

bool valid_machine_selection(const SearchSpec &spec, std::string *message) {
  const Suite &legacy = suite("26");
  if (spec.rotor_order.size() != 3) {
    *message = "Select exactly three Legacy rotors.";
    return false;
  }
  for (std::size_t i = 0; i < spec.rotor_order.size(); ++i) {
    if (std::find(legacy.rotor_names.begin(), legacy.rotor_names.end(),
                  spec.rotor_order[i]) == legacy.rotor_names.end()) {
      *message = "The rotor order contains an unknown Legacy rotor.";
      return false;
    }
    if (std::find(spec.rotor_order.begin(),
                  spec.rotor_order.begin() + static_cast<std::ptrdiff_t>(i),
                  spec.rotor_order[i]) !=
        spec.rotor_order.begin() + static_cast<std::ptrdiff_t>(i)) {
      *message = "The rotor order cannot contain duplicates.";
      return false;
    }
  }
  if (spec.reflector != "B") {
    *message = "This stage supports reflector B only.";
    return false;
  }
  if (spec.result_limit == 0 || spec.result_limit > 64) {
    *message = "The displayed stop limit must be from 1 to 64.";
    return false;
  }
  return true;
}

Machine make_machine(const SearchSpec &spec, const std::string &core,
                     const std::vector<std::string> &plugs) {
  const Suite &legacy = suite("26");
  Alphabet alphabet(legacy.alphabet);
  std::vector<Rotor> rotors;
  for (const std::string &name : spec.rotor_order)
    rotors.push_back(make_rotor(name, alphabet));
  Reflector reflector = make_reflector(spec.reflector, alphabet);
  Plugboard board(plugs, alphabet);
  Machine machine(alphabet, std::move(rotors), std::move(reflector),
                  std::move(board), std::vector<int>{1, 1, 1}, core + "A",
                  true);
  machine.set_moving_reflector(false);
  return machine;
}

using Scrambler = std::array<int, kAlphabetSize>;

std::vector<Scrambler> scramblers_for(const SearchSpec &spec,
                                      const std::vector<MenuLink> &menu,
                                      const std::string &core) {
  Machine position = make_machine(spec, core, {});
  std::size_t at = 0;
  std::vector<Scrambler> maps;
  maps.reserve(menu.size());
  for (const MenuLink &link : menu) {
    while (at <= link.position) {
      position.encipher("A");
      ++at;
    }
    Scrambler map{};
    const std::vector<Rotor> &rotors = position.rotors();
    const Reflector &reflector = position.reflector();
    for (int letter = 0; letter < kAlphabetSize; ++letter) {
      int signal = letter;
      for (std::size_t i = rotors.size(); i-- > 0;)
        signal = rotors[i].fwd_table()[signal];
      signal = reflector.table()[signal];
      for (const Rotor &rotor : rotors)
        signal = rotor.bwd_table()[signal];
      map[static_cast<std::size_t>(letter)] = signal;
    }
    maps.push_back(map);
  }
  return maps;
}

bool feasible_ten_pair_board(const std::array<int, kAlphabetSize> &assigned) {
  int pairs = 0;
  int unassigned = 0;
  for (int i = 0; i < kAlphabetSize; ++i) {
    const int partner = assigned[static_cast<std::size_t>(i)];
    if (partner < 0)
      ++unassigned;
    else if (partner > i)
      ++pairs;
  }
  return pairs <= 10 && pairs + unassigned / 2 >= 10;
}

bool propagate(const std::vector<MenuLink> &menu,
               const std::vector<Scrambler> &maps, int test_letter,
               int test_partner, Stop *stop) {
  std::array<std::array<bool, kAlphabetSize>, kAlphabetSize> board{};
  std::array<int, kAlphabetSize> assigned{};
  assigned.fill(-1);
  std::deque<std::pair<int, int>> pending;
  bool contradiction = false;

  auto energize = [&](int row, int column) {
    if (board[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)])
      return;
    const int known = assigned[static_cast<std::size_t>(row)];
    if (known >= 0 && known != column) {
      contradiction = true;
      return;
    }
    assigned[static_cast<std::size_t>(row)] = column;
    board[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
        true;
    pending.emplace_back(row, column);
  };

  energize(test_letter, test_partner);
  while (!pending.empty() && !contradiction) {
    const auto current = pending.front();
    pending.pop_front();
    energize(current.second, current.first);
    for (std::size_t i = 0; i < menu.size() && !contradiction; ++i) {
      const int plain = letter_index(menu[i].plain);
      const int cipher = letter_index(menu[i].cipher);
      if (plain == current.first)
        energize(cipher, maps[i][static_cast<std::size_t>(current.second)]);
      if (cipher == current.first)
        energize(plain, maps[i][static_cast<std::size_t>(current.second)]);
    }
  }
  if (contradiction || !feasible_ten_pair_board(assigned))
    return false;

  for (int i = 0; i < kAlphabetSize; ++i) {
    const int partner = assigned[static_cast<std::size_t>(i)];
    if (partner == i)
      stop->implied_self.push_back(static_cast<char>('A' + i));
    else if (partner > i) {
      std::string pair;
      pair.push_back(static_cast<char>('A' + i));
      pair.push_back(static_cast<char>('A' + partner));
      stop->implied_pairs.push_back(pair);
    }
  }
  return true;
}

std::string core_from_index(std::uint64_t index) {
  std::string core(3, 'A');
  core[0] = static_cast<char>('A' + (index / (26ULL * 26ULL)) % 26ULL);
  core[1] = static_cast<char>('A' + (index / 26ULL) % 26ULL);
  core[2] = static_cast<char>('A' + index % 26ULL);
  return core;
}

char plug_partner(char letter, const std::vector<std::string> &pairs) {
  for (const std::string &pair : pairs) {
    if (pair[0] == letter)
      return pair[1];
    if (pair[1] == letter)
      return pair[0];
  }
  return letter;
}

std::string order_text(const std::vector<std::string> &order) {
  std::ostringstream out;
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (i != 0)
      out << " ";
    out << order[i];
  }
  return out.str();
}

std::string serialized_stops(const std::vector<Stop> &stops) {
  std::ostringstream out;
  for (const Stop &stop : stops)
    out << format_stop(stop) << "\n";
  return out.str();
}

SearchResult search_orders(const SearchSpec &spec,
                           const std::vector<std::vector<std::string>> &orders,
                           const BatchLimits &limits,
                           const CancelCheck &cancelled,
                           const ProgressSink &progress) {
  SearchResult result;
  const auto started = std::chrono::steady_clock::now();
  result.total_orders = orders.size();
  if (orders.empty()) {
    result.state = SearchState::InvalidInput;
    result.message = "At least one rotor order is required.";
    return result;
  }
  if (limits.retained_stops_per_order == 0 ||
      limits.retained_stops_per_order > 64 ||
      limits.retained_stops_total == 0 || limits.retained_stops_total > 64) {
    result.state = SearchState::InvalidInput;
    result.message = "Batch stop limits must be from 1 to 64.";
    return result;
  }
  if (orders.size() > 60 ||
      kCorePositions >
          std::numeric_limits<std::uint64_t>::max() / orders.size()) {
    result.state = SearchState::InvalidInput;
    result.message = "The rotor order batch exceeds the historic limit.";
    return result;
  }
  result.total = kCorePositions * orders.size();
  result.order_summaries.reserve(orders.size());
  result.state = SearchState::NoResult;
  try {
    for (std::size_t i = 0; i < orders.size(); ++i) {
      if (cancelled && cancelled()) {
        result.state = SearchState::Cancelled;
        result.message = "Batched search cancelled between rotor orders.";
        break;
      }

      SearchSpec one = spec;
      one.rotor_order = orders[i];
      one.result_limit = limits.retained_stops_per_order;
      const std::uint64_t tested_before = result.tested;
      const std::uint64_t stops_before = result.stop_count;
      SearchResult one_result = search(
          one, cancelled, [&](const SearchProgress &value) {
            if (!progress)
              return;
            SearchProgress aggregate;
            aggregate.tested = tested_before + value.tested;
            aggregate.total = result.total;
            aggregate.stops = stops_before + value.stops;
            aggregate.rotor_order = orders[i];
            aggregate.order_index = i + 1;
            aggregate.completed_orders = i + value.completed_orders;
            aggregate.total_orders = orders.size();
            aggregate.order_tested = value.tested;
            aggregate.order_total = value.total;
            progress(aggregate);
          });

      if (result.menu.empty())
        result.menu = one_result.menu;
      result.tested += one_result.tested;
      result.stop_count += one_result.stop_count;
      OrderSummary summary;
      summary.rotor_order = orders[i];
      summary.tested = one_result.tested;
      summary.stop_count = one_result.stop_count;

      for (Stop &stop : one_result.stops) {
        if (result.stops.size() < limits.retained_stops_total) {
          result.stops.push_back(std::move(stop));
          ++summary.retained_stop_count;
        } else
          result.stops_truncated = true;
      }
      summary.stops_truncated =
          summary.retained_stop_count < summary.stop_count;
      result.order_summaries.push_back(summary);
      if (summary.stops_truncated)
        result.stops_truncated = true;

      if (one_result.state == SearchState::Cancelled) {
        result.state = SearchState::Cancelled;
        result.message = "Batched search cancelled within rotor order " +
                         order_text(orders[i]) + ".";
        break;
      }
      if (one_result.state == SearchState::InvalidInput ||
          one_result.state == SearchState::Failure) {
        result.state = one_result.state;
        result.message = one_result.message;
        break;
      }

      ++result.completed_orders;
    }

    if (result.state != SearchState::Cancelled &&
        result.state != SearchState::InvalidInput &&
        result.state != SearchState::Failure) {
      result.state =
          result.stop_count > 0 ? SearchState::Success : SearchState::NoResult;
      result.message =
          result.stop_count > 0
              ? "Batched Bombe run completed with " +
                    std::to_string(result.stop_count) + " stops counted and " +
                    std::to_string(result.stops.size()) +
                    " retained under the batch bounds."
              : "Batched Bombe run completed with no stops.";
    }
  } catch (const std::exception &error) {
    result.state = SearchState::Failure;
    result.message = error.what();
  } catch (...) {
    result.state = SearchState::Failure;
    result.message = "Unknown batched Bombe failure.";
  }
  result.seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  return result;
}

}

SearchResult search(const SearchSpec &spec, const CancelCheck &cancelled,
                    const ProgressSink &progress) {
  SearchResult result;
  result.total = kCorePositions;
  result.total_orders = 1;
  const auto started = std::chrono::steady_clock::now();
  try {
    if (!valid_machine_selection(spec, &result.message)) {
      result.state = SearchState::InvalidInput;
      return result;
    }
    MenuBuild built = build_menu(spec);
    result.menu = built.links;
    if (!built.ok) {
      result.state = SearchState::InvalidInput;
      result.message = built.message;
      return result;
    }

    if (progress) {
      SearchProgress value;
      value.total = kCorePositions;
      value.rotor_order = spec.rotor_order;
      value.order_index = 1;
      value.total_orders = 1;
      value.order_total = kCorePositions;
      progress(value);
    }
    for (std::uint64_t index = 0; index < kCorePositions; ++index) {
      if (cancelled && cancelled()) {
        result.state = SearchState::Cancelled;
        result.message = "Search cancelled.";
        break;
      }
      const std::string core = core_from_index(index);
      const std::vector<Scrambler> maps =
          scramblers_for(spec, result.menu, core);
      for (int partner = 0; partner < kAlphabetSize; ++partner) {
        Stop stop;
        stop.rotor_order = spec.rotor_order;
        stop.rotor_core = core;
        stop.test_letter = static_cast<char>('A' + built.test_letter);
        stop.test_partner = static_cast<char>('A' + partner);
        if (!propagate(result.menu, maps, built.test_letter, partner, &stop))
          continue;
        ++result.stop_count;
        if (result.stops.size() < spec.result_limit)
          result.stops.push_back(std::move(stop));
        else
          result.stops_truncated = true;
      }
      result.tested = index + 1;
      if (progress &&
          (result.tested % 64 == 0 || result.tested == result.total)) {
        SearchProgress value;
        value.tested = result.tested;
        value.total = result.total;
        value.stops = result.stop_count;
        value.rotor_order = spec.rotor_order;
        value.order_index = 1;
        value.completed_orders = result.tested == result.total ? 1 : 0;
        value.total_orders = 1;
        value.order_tested = result.tested;
        value.order_total = result.total;
        progress(value);
      }
    }
    if (result.state != SearchState::Cancelled) {
      result.state =
          result.stop_count > 0 ? SearchState::Success : SearchState::NoResult;
      result.completed_orders = 1;
      result.message = result.stop_count > 0
                           ? "Bombe run completed with stops."
                           : "Bombe run completed with no stops.";
    }
  } catch (const std::exception &error) {
    result.state = SearchState::Failure;
    result.message = error.what();
  } catch (...) {
    result.state = SearchState::Failure;
    result.message = "Unknown Bombe failure.";
  }
  result.seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  return result;
}

const std::vector<std::string> &approved_rotor_pool() {
  static const std::vector<std::string> pool{"I", "II", "III", "IV", "V"};
  return pool;
}

const std::vector<std::vector<std::string>> &approved_rotor_orders() {
  static const std::vector<std::vector<std::string>> orders = [] {
    std::vector<std::vector<std::string>> out;
    const std::vector<std::string> &pool = approved_rotor_pool();
    for (std::size_t a = 0; a < pool.size(); ++a)
      for (std::size_t b = 0; b < pool.size(); ++b) {
        if (b == a)
          continue;
        for (std::size_t c = 0; c < pool.size(); ++c) {
          if (c == a || c == b)
            continue;
          out.push_back({pool[a], pool[b], pool[c]});
        }
      }
    return out;
  }();
  return orders;
}

std::uint64_t approved_batch_positions() {
  return kCorePositions * approved_rotor_orders().size();
}

SearchResult search_batch(const SearchSpec &spec, const BatchLimits &limits,
                          const CancelCheck &cancelled,
                          const ProgressSink &progress) {
  return search_orders(spec, approved_rotor_orders(), limits, cancelled,
                       progress);
}

SearchSpec demonstration_spec() {
  SearchSpec spec;
  spec.rotor_order = {"II", "V", "III"};
  spec.reflector = "B";
  spec.crib = "DASXISTXEINXABSTIMMSPRUQ";
  spec.crib_offset = 0;
  spec.menu_limit = 12;
  spec.result_limit = 64;
  const std::vector<std::string> plugs{"AD", "ET", "HM", "JL", "NV",
                                       "FU", "GQ", "PZ", "OX", "IK"};
  Machine machine = make_machine(spec, "DKX", plugs);
  spec.ciphertext = machine.encipher(spec.crib);
  return spec;
}

std::string format_stop(const Stop &stop) {
  std::ostringstream out;
  if (!stop.rotor_order.empty())
    out << order_text(stop.rotor_order) << " | ";
  out << stop.rotor_core << "   " << stop.test_letter << "/"
      << stop.test_partner;
  if (!stop.implied_pairs.empty()) {
    out << "   pairs";
    for (const std::string &pair : stop.implied_pairs)
      out << " " << pair;
  }
  if (!stop.implied_self.empty()) {
    out << "   self";
    for (char letter : stop.implied_self)
      out << " " << letter;
  }
  return out.str();
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
  check(monotonic && last_progress == kCorePositions,
        "historic Bombe progress is monotonic and reaches the full run size");
  check(recovered.menu.size() >= 6 && recovered.menu.size() <= 12,
        "historic Bombe builds a bounded connected menu");

  const BatchLimits matching_limits{fixture.result_limit,
                                    fixture.result_limit};
  SearchResult one_order_batch =
      search_orders(fixture, {fixture.rotor_order}, matching_limits, {}, {});
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
      search_orders(fixture, no_result_orders, BatchLimits{}, {}, {});
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
  SearchResult between = search_orders(
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
  SearchResult within = search_orders(
      fixture, no_result_orders, BatchLimits{},
      [&] { return ++batch_cancel_checks > 16; }, {});
  check(within.state == SearchState::Cancelled &&
            within.completed_orders == 0 && within.tested < kCorePositions,
        "historic Bombe batch cancels within a rotor order");

  BatchLimits bounded_limits;
  bounded_limits.retained_stops_per_order = 1;
  bounded_limits.retained_stops_total = 1;
  SearchResult bounded = search_orders(
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

  SearchSpec crashed = fixture;
  crashed.crib = "AAAAAA";
  crashed.ciphertext = "AAAAAA";
  SearchResult invalid = search(crashed);
  check(invalid.state == SearchState::InvalidInput && invalid.tested == 0,
        "historic Bombe refuses a crashed crib alignment");
}

}
}

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

using PlugMap = std::array<int, kAlphabetSize>;

bool assign_plug(PlugMap *mapping, int a, int b) {
  if (a < 0 || a >= kAlphabetSize || b < 0 || b >= kAlphabetSize)
    return false;
  if ((*mapping)[static_cast<std::size_t>(a)] >= 0 &&
      (*mapping)[static_cast<std::size_t>(a)] != b)
    return false;
  if ((*mapping)[static_cast<std::size_t>(b)] >= 0 &&
      (*mapping)[static_cast<std::size_t>(b)] != a)
    return false;
  (*mapping)[static_cast<std::size_t>(a)] = b;
  (*mapping)[static_cast<std::size_t>(b)] = a;
  return true;
}

int plug_pair_count(const PlugMap &mapping) {
  int pairs = 0;
  for (int i = 0; i < kAlphabetSize; ++i)
    if (mapping[static_cast<std::size_t>(i)] > i)
      ++pairs;
  return pairs;
}

bool feasible_ten_pair_completion(const PlugMap &mapping) {
  int unassigned = 0;
  for (int value : mapping)
    if (value < 0)
      ++unassigned;
  const int pairs = plug_pair_count(mapping);
  return pairs <= 10 && pairs + unassigned / 2 >= 10;
}

bool fill_ten_pair_completion(PlugMap *mapping) {
  if (!feasible_ten_pair_completion(*mapping))
    return false;
  while (plug_pair_count(*mapping) < 10) {
    int first = -1;
    int second = -1;
    for (int i = 0; i < kAlphabetSize && second < 0; ++i) {
      if ((*mapping)[static_cast<std::size_t>(i)] >= 0)
        continue;
      if (first < 0)
        first = i;
      else
        second = i;
    }
    if (first < 0 || second < 0 || !assign_plug(mapping, first, second))
      return false;
  }
  for (int i = 0; i < kAlphabetSize; ++i)
    if ((*mapping)[static_cast<std::size_t>(i)] < 0)
      (*mapping)[static_cast<std::size_t>(i)] = i;
  return plug_pair_count(*mapping) == 10;
}

std::vector<std::string> plug_pairs(const PlugMap &mapping) {
  std::vector<std::string> pairs;
  for (int i = 0; i < kAlphabetSize; ++i) {
    const int partner = mapping[static_cast<std::size_t>(i)];
    if (partner > i) {
      std::string pair;
      pair.push_back(static_cast<char>('A' + i));
      pair.push_back(static_cast<char>('A' + partner));
      pairs.push_back(pair);
    }
  }
  return pairs;
}

bool check_stop(const SearchSpec &spec, const Stop &raw, Stop *checked) {
  std::vector<MenuLink> crib_links;
  crib_links.reserve(spec.crib.size());
  for (std::size_t i = 0; i < spec.crib.size(); ++i)
    crib_links.push_back(MenuLink{spec.crib[i],
                                  spec.ciphertext[spec.crib_offset + i],
                                  spec.crib_offset + i});
  const std::vector<Scrambler> maps =
      scramblers_for(spec, crib_links, raw.rotor_core);

  PlugMap initial;
  initial.fill(-1);
  if (!assign_plug(&initial, letter_index(raw.test_letter),
                   letter_index(raw.test_partner)))
    return false;
  for (const std::string &pair : raw.implied_pairs) {
    if (pair.size() != 2 ||
        !assign_plug(&initial, letter_index(pair[0]), letter_index(pair[1])))
      return false;
  }
  for (char letter : raw.implied_self)
    if (!assign_plug(&initial, letter_index(letter), letter_index(letter)))
      return false;

  std::function<bool(std::size_t, const PlugMap &, PlugMap *)> visit =
      [&](std::size_t position, const PlugMap &mapping,
          PlugMap *solution) -> bool {
    if (!feasible_ten_pair_completion(mapping))
      return false;
    if (position == crib_links.size()) {
      PlugMap completed = mapping;
      if (!fill_ten_pair_completion(&completed))
        return false;
      Machine machine = make_machine(spec, raw.rotor_core,
                                     plug_pairs(completed));
      std::string input(spec.crib_offset + spec.crib.size(), 'A');
      input.replace(spec.crib_offset, spec.crib.size(), spec.crib);
      const std::string output = machine.encipher(input);
      if (output.compare(spec.crib_offset, spec.crib.size(),
                         spec.ciphertext, spec.crib_offset,
                         spec.crib.size()) != 0)
        return false;
      *solution = completed;
      return true;
    }

    const int plain = letter_index(crib_links[position].plain);
    const int cipher = letter_index(crib_links[position].cipher);
    const Scrambler &map = maps[position];
    const int known = mapping[static_cast<std::size_t>(plain)];
    if (known >= 0) {
      PlugMap next = mapping;
      if (!assign_plug(&next, map[static_cast<std::size_t>(known)], cipher))
        return false;
      return visit(position + 1, next, solution);
    }

    for (int candidate = 0; candidate < kAlphabetSize; ++candidate) {
      PlugMap next = mapping;
      if (!assign_plug(&next, plain, candidate) ||
          !assign_plug(&next,
                       map[static_cast<std::size_t>(candidate)], cipher))
        continue;
      if (visit(position + 1, next, solution))
        return true;
    }
    return false;
  };

  PlugMap solution;
  if (!visit(0, initial, &solution))
    return false;
  *checked = raw;
  checked->check_state = StopCheckState::Checked;
  checked->completed_pairs = plug_pairs(solution);
  return true;
}

std::string core_from_index(std::uint64_t index) {
  std::string core(3, 'A');
  core[0] = static_cast<char>('A' + (index / (26ULL * 26ULL)) % 26ULL);
  core[1] = static_cast<char>('A' + (index / 26ULL) % 26ULL);
  core[2] = static_cast<char>('A' + index % 26ULL);
  return core;
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
          if (stop.check_state == StopCheckState::Checked) {
            ++result.checked_stop_count;
            result.checked_stops.push_back(stop);
          } else if (stop.check_state == StopCheckState::Rejected) {
            ++result.rejected_stop_count;
          }
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
          result.checked_stop_count > 0 ? SearchState::Success
                                        : SearchState::NoResult;
      result.message =
          result.checked_stop_count > 0
              ? "Batched Bombe checking completed with " +
                    std::to_string(result.checked_stop_count) +
                    " checked stops from " + std::to_string(result.stop_count) +
                    " raw stops counted."
              : result.stop_count > 0
                    ? "Batched Bombe checking rejected " +
                          std::to_string(result.rejected_stop_count) +
                          " retained raw stops from " +
                          std::to_string(result.stop_count) +
                          " raw stops counted."
                    : "Batched Bombe run completed with no raw stops.";
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
        if (result.stops.size() < spec.result_limit) {
          Stop checked;
          if (check_stop(spec, stop, &checked)) {
            stop = checked;
            ++result.checked_stop_count;
            result.checked_stops.push_back(checked);
          } else {
            stop.check_state = StopCheckState::Rejected;
            ++result.rejected_stop_count;
          }
          result.stops.push_back(std::move(stop));
        } else {
          result.stops_truncated = true;
        }
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
          result.checked_stop_count > 0 ? SearchState::Success
                                        : SearchState::NoResult;
      result.completed_orders = 1;
      result.message =
          result.checked_stop_count > 0
              ? "Bombe checking completed with " +
                    std::to_string(result.checked_stop_count) +
                    " checked stops from " + std::to_string(result.stop_count) +
                    " raw stops counted."
              : result.stop_count > 0
                    ? "Bombe checking rejected " +
                          std::to_string(result.rejected_stop_count) +
                          " retained raw stops from " +
                          std::to_string(result.stop_count) +
                          " raw stops counted."
                    : "Bombe run completed with no raw stops.";
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

SearchResult search_order_batch(
    const SearchSpec &spec,
    const std::vector<std::vector<std::string>> &orders,
    const BatchLimits &limits,
    const CancelCheck &cancelled,
    const ProgressSink &progress) {
  return search_orders(spec, orders, limits, cancelled, progress);
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
  if (stop.check_state == StopCheckState::Checked) {
    out << "   CHECKED pairs";
    for (const std::string &pair : stop.completed_pairs)
      out << " " << pair;
  } else if (stop.check_state == StopCheckState::Rejected) {
    out << "   REJECTED";
  }
  return out.str();
}


}
}

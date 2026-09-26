#include <cstdio>
#include <iostream>
#include <string>

#include "generator.hpp"
#include "gui.hpp"
#include "gui_bombe_panel.hpp"
#include "gui_enciphering_panel.hpp"
#include "gui_legal_panel.hpp"
#include "gui_setup_panel.hpp"
#include "gui_widgets.hpp"
#include "registry.hpp"
#include "compatibility_fixtures.hpp"
#include "gui_checks.hpp"

int main(int argc, char** argv) {
    const inop::Suite& active_suite = inop::suite("38");
    if (argc == 2 && std::string(argv[1]) == "--write-synthetic-catalogue") {
        std::string error;
        inop::WheelBatch rotors = inop::build_wheel_batch(active_suite, true, 10, 950, 2);
        inop::WheelBatch reflectors = inop::build_wheel_batch(active_suite, false, 2, 950, 0);
        const bool wrote =
            inop::write_wheel_batch("inop_rotors.json", rotors, active_suite, false, &error) &&
            inop::write_wheel_batch("inop_reflectors.json", reflectors, active_suite, false, &error);
        if (!wrote) std::cerr << "synthetic catalogue generation failed: " << error << '\n';
        return wrote ? 0 : 1;
    }
    int failures = 0;
    auto check = [&](bool ok, const std::string& name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok) ++failures;
    };

    const std::string catalogue = "inop_gui_headless_catalogue.json";
    std::string error;
    inop::WheelBatch rotors = inop::build_wheel_batch(active_suite, true, 10, 950, 2);
    inop::WheelBatch reflectors = inop::build_wheel_batch(active_suite, false, 2, 950, 0);
    const bool wrote = inop::write_wheel_batch(catalogue, rotors, active_suite, false, &error) &&
                       inop::write_wheel_batch(catalogue, reflectors, active_suite, true, &error);
    const int loaded = wrote ? inop::load_wheel_file(catalogue) : 0;
    std::remove(catalogue.c_str());
    check(wrote && loaded == 12, "synthetic GUI catalogue loads");

    const std::string group = argc == 3 && std::string(argv[1]) == "--group" ? argv[2] : "full";
    if (group == "aggregate") {
        inop::gui_aggregate_self_test(check);
        return failures == 0 ? 0 : 1;
    }
    if (group == "widgets") {
        inop::gui::text_edit_self_test(check);
        inop::gui::scroll_region_self_test(check);
        inop::gui::dropdown_search_self_test(check);
        return failures == 0 ? 0 : 1;
    }
    if (group == "panels") {
        inop::gui::BombePanel::self_test(check);
        inop::gui::LegalPanel::self_test(check);
        inop::gui::EncipheringPanel::self_test(check);
        return failures == 0 ? 0 : 1;
    }
    if (group != "full") {
        std::cerr << "unknown GUI check group: " << group << '\n';
        return 2;
    }

    inop::gui_self_test(check);

    inop::gui::PanelState state;
    state.suite_code = "38";
    state.language_code = "eng";
    state.rotor_count = 5;
    for (int i = 0; i < state.rotor_count; ++i) {
        state.rotor_rows[i].rotor_name = "U" + std::to_string(950 + i);
        state.rotor_rows[i].ring_text = std::to_string(i + 1);
        if (i > 0) state.rotor_rows[i].notch_box[0] = std::string(1, static_cast<char>('a' + i));
    }
    state.reflector_name = "K950";
    state.master_key_text = "abcde0";
    state.marker_text = "abcdefghijklmnop";

    state.rotor_rows[0].notch_box[3] = "a";
    inop::gui::FieldValidity fourth = inop::gui::derive_validity(state);
    check(fourth.notch_ok[0], "fourth notch field works as the sole entry");

    state.rotor_rows[0].notch_box[3].clear();
    state.rotor_rows[0].notch_box[4] = "a";
    inop::gui::FieldValidity fifth = inop::gui::derive_validity(state);
    check(fifth.notch_ok[0], "fifth notch field works as the sole entry");

    run_compatibility_fixtures(check);

    return failures == 0 ? 0 : 1;
}

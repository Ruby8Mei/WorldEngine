#include "compatibility_fixtures.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_compat.hpp"
#include "generator.hpp"
#include "gui_config_store.hpp"
#include "gui_prefs.hpp"
#include "gui_setup_panel.hpp"
#include "pipeline.hpp"
#include "registry.hpp"
#include "settings.hpp"

namespace {

std::string read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream data;
    data << input.rdbuf();
    return data.str();
}

bool write_bytes(const std::string& path, const std::string& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << data;
    return static_cast<bool>(output);
}

inop::Settings current_settings(bool marker = true) {
    inop::Settings settings;
    settings.suite_code = "38";
    settings.rotors = {"U950", "U951", "U952", "U953", "U954"};
    settings.reflector = "K950";
    settings.rings = {1, 2, 3, 4, 5};
    settings.notches = {"a", "b", "c", "d", "e"};
    settings.plugs = {"fg", "hi"};
    settings.master_key = "abcde0";
    if (marker) settings.marker = "abcdefghijklmnop";
    return settings;
}

nlohmann::json settings_json(bool marker = true) {
    const inop::Settings settings = current_settings(marker);
    nlohmann::json document;
    document["suite_code"] = settings.suite_code;
    document["reflector"] = settings.reflector;
    document["master_key"] = settings.master_key;
    if (marker) document["marker"] = settings.marker;
    document["rotor_count"] = 5;
    document["rotors"] = nlohmann::json::array();
    for (size_t i = 0; i < settings.rotors.size(); ++i) {
        document["rotors"].push_back({
            {"name", settings.rotors[i]},
            {"ring", i == 0 ? nlohmann::json(1) : nlohmann::json(std::to_string(i + 1))},
            {"notches", settings.notches[i]}
        });
    }
    document["plugboard"] = settings.plugs;
    return document;
}

inop::gui::PanelState panel_state(bool marker = true) {
    inop::gui::PanelState state;
    state.suite_code = "38";
    state.language_code = "eng";
    state.rotor_count = 5;
    for (int i = 0; i < state.rotor_count; ++i) {
        state.rotor_rows[i].rotor_name = "U" + std::to_string(950 + i);
        state.rotor_rows[i].ring_text = std::to_string(i + 1);
        state.rotor_rows[i].notch_box[0] = std::string(1, static_cast<char>('a' + i));
    }
    state.reflector_name = "K950";
    state.plug_left[0] = "f";
    state.plug_right[0] = "g";
    state.master_key_text = "abcde0";
    if (marker) state.marker_text = "abcdefghijklmnop";
    return state;
}

nlohmann::json panel_json(bool marker = true) {
    const inop::gui::PanelState state = panel_state(marker);
    nlohmann::json setup;
    setup["suite_code"] = state.suite_code;
    setup["language_code"] = state.language_code;
    setup["rotor_count"] = state.rotor_count;
    setup["rotors"] = nlohmann::json::array();
    for (int i = 0; i < state.rotor_count; ++i) {
        setup["rotors"].push_back({
            {"name", state.rotor_rows[i].rotor_name},
            {"ring", state.rotor_rows[i].ring_text},
            {"notches", inop::gui::notch_text(state.rotor_rows[i])}
        });
    }
    setup["reflector"] = state.reflector_name;
    setup["plugboard"] = nlohmann::json::array({"fg"});
    setup["double_pass"] = true;
    setup["padding"] = true;
    setup["moving_reflector"] = true;
    setup["master_key"] = state.master_key_text;
    if (marker) setup["marker"] = state.marker_text;
    return setup;
}

std::string old_settings_text() {
    return "suite 38\n"
           "rotors U950 U951 U952 U953 U954\n"
           "reflector K950\n"
           "rings 1 2 3 4 5\n"
           "notches a b c d e\n"
           "plugs fg hi\n"
           "key abcde0\n";
}

}

void run_compatibility_fixtures(const inop::SelfTestCheck& check) {
    bool all_rows = true;
    auto row = [&](bool ok, const std::string& name) {
        all_rows = all_rows && ok;
        check(ok, name);
    };

    {
        inop::Settings legacy;
        legacy.suite_code = "26";
        legacy.rotors = {"I", "II", "III"};
        legacy.reflector = "B";
        legacy.rings = {1, 1, 1};
        legacy.master_key = "AAA";
        std::string error;
        inop::Machine machine = inop::build_machine(legacy);
        inop::PipelineConfig config;
        const bool locked = inop::apply_suite_lock(config, true, 5);
        inop::Pipeline pipeline(machine, config);
        const inop::Encrypted encrypted = pipeline.encrypt("LEGACYTESTAA");
        row(inop::validate_settings(legacy, &error) && locked && !config.double_pass &&
                !config.padding && !config.moving_reflector && config.block == 5 &&
                encrypted.ciphertext.size() == 12 &&
                pipeline.decrypt(encrypted.ciphertext) == "LEGACYTESTAA",
            "compat Legacy suite remains marker free and historically locked");
    }

    {
        inop::Settings base;
        base.suite_code = "26";
        base.rotors = {"I", "II", "III"};
        base.reflector = "B";
        base.rings = {1, 1, 1};
        base.master_key = "AAA";
        inop::Settings extra = base;
        extra.master_key = "AAAZ";
        std::string note;
        inop::Machine first = inop::build_machine(base);
        inop::Machine second = inop::build_machine(extra, &note);
        row(first.encipher("AAAAAAAAAAAA") == second.encipher("AAAAAAAAAAAA") && !note.empty(),
            "compat Legacy extra reflector key symbol is ignored with a note");
    }

    {
        const inop::Suite& active_suite = inop::suite("38");
        inop::WheelBatch rotor = inop::build_wheel_batch(active_suite, true, 1, 980, 2);
        inop::WheelBatch reflector = inop::build_wheel_batch(active_suite, false, 1, 980, 0);
        const std::string old_path = "inop_compat_wheels.txt";
        const std::string rotor_path = "inop_compat_rotors.json";
        const std::string reflector_path = "inop_compat_reflectors.json";
        const std::string source = "rotor " + rotor.wheels[0].name + " " +
                                   rotor.wheels[0].wiring + " " + rotor.wheels[0].notches +
                                   "\nreflector " + reflector.wheels[0].name + " " +
                                   reflector.wheels[0].wiring + "\n";
        write_bytes(old_path, source);
        std::vector<std::string> problems;
        const int migrated = inop::migrate_wheels_from_text(
            old_path, rotor_path, reflector_path, &problems);
        const std::string modern_before = read_bytes(rotor_path);
        const int repeated = inop::migrate_wheels_from_text(
            old_path, rotor_path, reflector_path, &problems);
        row(migrated == 2 && repeated == 0 && read_bytes(old_path) == source &&
                read_bytes(rotor_path) == modern_before &&
                inop::validate_wheel_file(rotor_path) &&
                inop::validate_wheel_file(reflector_path),
            "compat 2.2 wheel migration splits files and preserves every source");
        std::remove(old_path.c_str());
        std::remove(rotor_path.c_str());
        std::remove(reflector_path.c_str());
    }

    {
        const std::string old_path = "inop_compat_old.settings";
        const std::string new_path = "inop_compat_settings.json";
        const std::string source = old_settings_text();
        write_bytes(old_path, source);
        const bool migrated = inop::migrate_settings_from_text(old_path, new_path);
        const std::string modern_before = read_bytes(new_path);
        const bool repeated = inop::migrate_settings_from_text(old_path, new_path);
        inop::Settings loaded;
        std::string error;
        bool marker_missing = false;
        const bool accepted = inop::load_settings(loaded, new_path, &error, &marker_missing);
        row(migrated && !repeated && accepted && marker_missing && loaded.marker.empty() &&
                read_bytes(old_path) == source && read_bytes(new_path) == modern_before,
            "compat 2.2 settings migration preserves the original and modern target");
        std::remove(old_path.c_str());
        std::remove(new_path.c_str());
    }

    {
        const std::string path = "inop_compat_old_keysheet.txt";
        const std::string source = old_settings_text() + "\n" + old_settings_text();
        write_bytes(path, source);
        row(inop::count_keysheet_entries(path) == 0 && read_bytes(path) == source,
            "compat old text key sheets remain unsupported and untouched");
        std::remove(path.c_str());
    }

    {
        const std::string path = "inop_compat_current_settings.json";
        nlohmann::json document = settings_json();
        document["future_keep"] = { {"value", 7} };
        const std::string source = document.dump(2) + "\n";
        write_bytes(path, source);
        inop::Settings loaded;
        std::string error;
        const bool accepted = inop::load_settings(loaded, path, &error);
        const bool saved = accepted && inop::save_settings(loaded, path);
        const nlohmann::json after = nlohmann::json::parse(read_bytes(path), nullptr, false);
        row(accepted && saved && loaded.rings == std::vector<int>({1, 2, 3, 4, 5}) &&
                after.contains("future_keep") && after["future_keep"]["value"] == 7,
            "compat current settings accept number or string rings and preserve unknown fields");
        std::remove(path.c_str());
    }

    {
        const std::string path = "inop_compat_missing_marker.json";
        const std::string source = settings_json(false).dump();
        write_bytes(path, source);
        inop::Settings loaded;
        std::string error;
        bool marker_missing = false;
        const bool accepted = inop::load_settings(loaded, path, &error, &marker_missing);
        row(accepted && marker_missing && loaded.marker.empty() && read_bytes(path) == source,
            "compat settings missing a marker import incomplete without source changes");
        std::remove(path.c_str());
    }

    {
        const std::string path = "inop_compat_keysheet.json";
        nlohmann::json invalid = settings_json();
        invalid["master_key"] = "bad";
        nlohmann::json sheet;
        sheet["entries"] = nlohmann::json::array({settings_json(), invalid, settings_json()});
        const std::string source = sheet.dump();
        write_bytes(path, source);
        std::vector<inop::KeySheetEntry> entries;
        std::string error;
        inop::Settings third;
        const bool loaded = inop::load_keysheet(path, entries, &error);
        const bool selected = inop::load_keysheet_entry(path, 3, third, &error);
        inop::Settings zero;
        const bool zero_refused = !inop::load_keysheet_entry(path, 0, zero, &error);
        row(loaded && entries.size() == 3 && entries[0].valid && !entries[1].valid &&
                entries[1].error.find("entry 2") != std::string::npos && entries[2].valid &&
                selected && third.master_key == "abcde0" && zero_refused &&
                read_bytes(path) == source,
            "compat current key sheet keeps per entry errors and one based selection");
        std::remove(path.c_str());
    }

    {
        const std::string path = "inop_compat_keysheet_marker.json";
        nlohmann::json sheet;
        sheet["entries"] = nlohmann::json::array({settings_json(false)});
        const std::string source = sheet.dump();
        write_bytes(path, source);
        inop::Settings loaded;
        std::string error;
        bool marker_missing = false;
        const bool accepted = inop::load_keysheet_entry(path, 1, loaded, &error, &marker_missing);
        row(accepted && marker_missing && loaded.marker.empty() && read_bytes(path) == source,
            "compat key sheet entry missing a marker imports incomplete and unchanged");
        std::remove(path.c_str());
    }

    {
        const std::string name = "compat-v2.inop";
        std::string error;
        const bool saved = inop::gui::save_config(panel_state(), name, &error);
        inop::gui::PanelState loaded;
        const bool accepted = saved && inop::gui::load_config("setup/" + name, loaded, &error);
        nlohmann::json strict = nlohmann::json::parse(read_bytes("setup/" + name));
        strict["extra"] = true;
        const std::string bad_path = "inop_compat_v2_extra.inop";
        write_bytes(bad_path, strict.dump());
        inop::gui::PanelState unchanged;
        unchanged.suite_code = "26";
        const bool rejected = !inop::gui::load_config(bad_path, unchanged, &error);
        row(accepted && rejected && unchanged.suite_code == "26",
            "compat version 2 preset accepts exact fields and rejects extras without state changes");
        std::string delete_error;
        if (saved) inop::gui::delete_config("setup/" + name, &delete_error);
        std::remove(bad_path.c_str());
    }

    {
        nlohmann::json document;
        document["format"] = inop::gui::kSetupFormatIdentity;
        document["version"] = 1;
        document["setup"] = panel_json(false);
        const std::string path = "inop_compat_v1.inop";
        const std::string source = document.dump();
        write_bytes(path, source);
        inop::gui::PanelState loaded;
        std::string error;
        bool migration = false;
        const bool accepted = inop::gui::load_config(path, loaded, &error, &migration);
        const bool overwrite_refused = !inop::gui::save_config(loaded, "compat-v1-source.inop", &error);
        row(accepted && migration && loaded.marker_text.empty() && overwrite_refused &&
                read_bytes(path) == source,
            "compat version 1 preset is read only until explicit marker completion");
        std::remove(path.c_str());
    }

    {
        const std::string path = "inop_compat_raw.json";
        const std::string source = panel_json(false).dump();
        write_bytes(path, source);
        inop::gui::PanelState loaded;
        std::string error;
        bool migration = false;
        const bool accepted = inop::gui::load_config(path, loaded, &error, &migration);
        const bool overwrite_refused = !inop::gui::save_config(loaded, "compat-raw.json", &error);
        row(accepted && migration && overwrite_refused && read_bytes(path) == source,
            "compat raw JSON setup is read only and preserves its source");
        std::remove(path.c_str());
    }

    {
        nlohmann::json document;
        document["format"] = inop::gui::kSetupFormatIdentity;
        document["version"] = 99;
        document["setup"] = panel_json();
        const std::string path = "inop_compat_future.inop";
        const std::string source = document.dump();
        write_bytes(path, source);
        inop::gui::PanelState unchanged;
        unchanged.suite_code = "26";
        unchanged.rotor_count = 3;
        std::string error;
        const bool rejected = !inop::gui::load_config(path, unchanged, &error);
        row(rejected && unchanged.suite_code == "26" && unchanged.rotor_count == 3 &&
                read_bytes(path) == source,
            "compat future preset version is rejected without source or active state changes");
        std::remove(path.c_str());
    }

    {
        const inop::Settings settings = current_settings();
        inop::Machine old_machine = inop::build_machine(settings);
        inop::Machine current_machine = inop::build_machine(settings);
        inop::PipelineConfig old_config;
        old_config.marker = "ponmlkjihgfedcba";
        inop::PipelineConfig current_config;
        current_config.marker = settings.marker;
        inop::Pipeline old_pipeline(old_machine, old_config);
        inop::Pipeline current_pipeline(current_machine, current_config);
        const inop::Encrypted encrypted = old_pipeline.encrypt("compatibility message");
        bool normal_rejected = false;
        try {
            current_pipeline.decrypt(encrypted.ciphertext);
        } catch (const std::exception&) {
            normal_rejected = true;
        }
        bool explicit_accepted = false;
        try {
            explicit_accepted = current_pipeline.decrypt_with_marker(
                encrypted.ciphertext, old_config.marker) == "compatibility message";
        } catch (const std::exception&) {
            explicit_accepted = false;
        }
        row(normal_rejected && explicit_accepted,
            "compat older ciphertext requires its explicit separate marker");
    }

    {
        const inop::Settings settings = current_settings();
        inop::Machine old_machine = inop::build_machine(settings);
        inop::Machine current_machine = inop::build_machine(settings);
        inop::PipelineConfig old_config;
        old_config.marker = settings.marker;
        old_config.double_pass = false;
        inop::PipelineConfig current_config;
        current_config.marker = settings.marker;
        current_config.double_pass = true;
        inop::Pipeline old_pipeline(old_machine, old_config);
        inop::Pipeline current_pipeline(current_machine, current_config);
        const inop::Encrypted encrypted = old_pipeline.encrypt("unsupported historic procedure");
        bool rejected = false;
        try {
            current_pipeline.decrypt(encrypted.ciphertext);
        } catch (const std::exception&) {
            rejected = true;
        }
        row(rejected, "compat unsupported historic cipher procedure has no automatic fallback");
    }

    {
        std::string uppercase = inop::suite("38").alphabet;
        for (char& symbol : uppercase)
            symbol = static_cast<char>(std::toupper(static_cast<unsigned char>(symbol)));
        nlohmann::json document;
        document["rotors"] = nlohmann::json::array({
            {{"name", "UPPERCASE"}, {"wiring", uppercase}, {"notches", "A"}}
        });
        std::vector<std::string> problems;
        row(!inop::validate_wheel_document(document.dump(), &problems) && !problems.empty(),
            "compat old uppercase INOP wheel convention remains rejected");
    }

    {
        const inop::Suite& active_suite = inop::suite("38");
        const std::string path = "inop_compat_names.json";
        inop::WheelBatch existing = inop::build_wheel_batch(active_suite, true, 1, 990, 2);
        existing.wheels[0].name = "CUSTOM-NAME";
        inop::WheelBatch appended = inop::build_wheel_batch(active_suite, true, 1, 991, 2);
        std::string error;
        const bool wrote = inop::write_wheel_batch(path, existing, active_suite, false, &error);
        const bool added = wrote && inop::write_wheel_batch(path, appended, active_suite, true, &error);
        const nlohmann::json document = nlohmann::json::parse(read_bytes(path), nullptr, false);
        bool custom_found = false;
        bool canonical_found = false;
        if (!document.is_discarded() && document.contains("rotors")) {
            for (const auto& wheel : document["rotors"]) {
                custom_found = custom_found || wheel.value("name", "") == "CUSTOM-NAME";
                canonical_found = canonical_found || wheel.value("name", "") == "U991";
            }
        }
        row(added && custom_found && canonical_found,
            "compat append preserves noncanonical names and adds canonical identifiers");
        std::remove(path.c_str());
    }

    {
        const std::string missing_path = "inop_compat_old_prefs.json";
        write_bytes(missing_path, "{}");
        inop::gui::GuiPrefs missing;
        missing.vsync = false;
        missing.frame_rate_limit = 30;
        const bool defaults = inop::gui::load_prefs(missing, missing_path);
        const std::string colours_path = "inop_compat_colour_prefs.json";
        write_bytes(colours_path, R"({"colourblind":"red-green"})");
        inop::gui::GuiPrefs red_green;
        const bool first = inop::gui::load_prefs(red_green, colours_path);
        write_bytes(colours_path, R"({"colourblind":"off"})");
        inop::gui::GuiPrefs off;
        const bool second = inop::gui::load_prefs(off, colours_path);
        row(defaults && missing.vsync && missing.frame_rate_limit == 0,
            "compat preferences missing V Sync and frame fields retain defaults");
        row(first && second && red_green.colourblind == inop::gui::ColourblindMode::Deuteranopia &&
                off.colourblind == inop::gui::ColourblindMode::Full,
            "compat older preference colour names remain accepted");
        std::remove(missing_path.c_str());
        std::remove(colours_path.c_str());
    }

    {
        const std::vector<std::pair<std::string, inop::CliCommand>> cases = {
            {":q", inop::CliCommand::Quit}, {":quit", inop::CliCommand::Quit},
            {":exit", inop::CliCommand::Quit}, {":i", inop::CliCommand::Info},
            {":info", inop::CliCommand::Info}, {":s", inop::CliCommand::Save},
            {":save", inop::CliCommand::Save}, {":d", inop::CliCommand::Decrypt},
            {":decrypt", inop::CliCommand::Decrypt}, {":d-old", inop::CliCommand::DecryptOld},
            {":decrypt-old", inop::CliCommand::DecryptOld}, {":b", inop::CliCommand::Batch},
            {":batch", inop::CliCommand::Batch}, {":?", inop::CliCommand::Help},
            {":h", inop::CliCommand::Help}, {":help", inop::CliCommand::Help}
        };
        bool aliases = true;
        for (const auto& item : cases) {
            aliases = aliases && inop::parse_cli_command(item.first) == item.second;
            std::string upper = item.first;
            for (char& symbol : upper)
                symbol = static_cast<char>(std::toupper(static_cast<unsigned char>(symbol)));
            aliases = aliases && inop::parse_cli_command(upper) == item.second;
        }
        aliases = aliases && inop::parse_cli_command(":unknown") == inop::CliCommand::Unknown &&
                  inop::parse_cli_command("plain text") == inop::CliCommand::Message;
        std::vector<std::string> tagged = {"ciphertext", "ENG"};
        const std::string language = inop::take_trailing_language_tag(tagged, false);
        std::vector<std::string> unsupported = {"ciphertext", "zzz"};
        const std::string unsupported_language =
            inop::take_trailing_language_tag(unsupported, false);
        std::vector<std::string> historic = {"ciphertext", "eng"};
        const std::string historic_language = inop::take_trailing_language_tag(historic, true);
        row(aliases && language == "eng" && tagged.size() == 1 &&
                unsupported_language.empty() && unsupported.size() == 2 &&
                historic_language.empty() && historic.size() == 2,
            "compat CLI aliases case rules unknown commands and trailing language tags remain stable");
    }

    row(inop::gui::developer_presets().size() == 3,
        "compat developer presets expose the public benchmark set");
    check(all_rows, "compat E 012 keeps every existing reader behind the policy gate");
}
